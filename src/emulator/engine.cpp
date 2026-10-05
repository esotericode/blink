#include "emulator/engine.hpp"
#include "teaching_rom.hpp"
#include "gb.h"
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <thread>

namespace observatory {
std::uint16_t canonicalAddress(std::uint16_t address) {
    return address >= 0xE000 && address < 0xFE00 ? address - 0x2000 : address;
}
struct Engine::Impl {
    GB_gameboy_t gb{};
    std::thread::id owner = std::this_thread::get_id();
    std::size_t capacity;
    std::vector<std::uint8_t> rom;
    std::array<std::uint32_t, screenPixels> rendering{}, completed{};
    std::uint64_t ticks{}, instructions{}, frames{}, frameTick{}, eventId{}, evicted{};
    std::string frameKind = "No completed frame";
    bool traceEnabled = true, teaching{}, frameArrived{}, inited{};
    std::optional<Instruction> active, lastExecuted;
    std::deque<WriteEvent> history;
    std::array<WriteEvent, 8> pending{};
    std::size_t pendingCount{};
    Activity activity;

    explicit Impl(std::size_t limit) : capacity(std::clamp<std::size_t>(limit, 8, 65536)) {}
    ~Impl() { if (inited) GB_free(&gb); }
    void assertOwner() const {
        if (std::this_thread::get_id() != owner) throw std::logic_error("Emulator used from a non-owning thread");
    }
    static Impl& self(GB_gameboy_t* instance) {
        return *static_cast<Impl*>(GB_get_user_data(instance));
    }
    static std::uint32_t encode(GB_gameboy_t*, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        return 0xFF000000u | std::uint32_t(r) << 16 | std::uint32_t(g) << 8 | b;
    }
    static void vblank(GB_gameboy_t* instance, GB_vblank_type_t type) {
        auto& s = self(instance);
        s.completed = s.rendering;
        ++s.frames;
        s.frameArrived = true;
        switch (type) {
        case GB_VBLANK_TYPE_NORMAL_FRAME: s.frameKind = "VBlank frame"; break;
        case GB_VBLANK_TYPE_LCD_OFF: s.frameKind = "LCD-off output"; break;
        case GB_VBLANK_TYPE_REPEAT: s.frameKind = "Retained output"; break;
        default: s.frameKind = "Artificial output"; break;
        }
        // frameTick is finalized at the enclosing GB_run boundary, not the callback cycle.
    }
    Instruction instruction(std::uint16_t pc) const {
        Instruction i{pc, {}};
        for (unsigned j = 0; j < i.bytes.size(); ++j) {
            i.bytes[j] = inspect(std::uint16_t(pc + j));
        }
        for (int j = 0; j < instructionLength(i.bytes[0]); ++j) i.available &= available(std::uint16_t(pc + j));
        return i;
    }
    static void execution(GB_gameboy_t* instance, std::uint16_t pc, std::uint8_t opcode) {
        auto& s = self(instance);
        ++s.instructions;
        ++s.activity.instructions;
        auto i = s.instruction(pc);
        i.bytes[0] = opcode; // Actual fetched opcode; operands are storage observations.
        s.active = i;
        s.lastExecuted = i;
    }
    bool physical(std::uint16_t address) const {
        auto a = canonicalAddress(address);
        return (a >= 0x8000 && a < 0xA000) || (a >= 0xC000 && a < 0xE000) ||
               (a >= 0xFE00 && a < 0xFEA0) || (a >= 0xFF80 && a < 0xFFFF);
    }
    bool available(std::uint16_t address) const {
        auto a = canonicalAddress(address);
        return a < 0xA000 || (a >= 0xC000 && a < 0xE000) ||
               (a >= 0xFE00 && a < 0xFEA0) || a >= 0xFF00;
    }
    std::uint8_t inspect(std::uint16_t address) const {
        auto a = canonicalAddress(address);
        auto* instance = const_cast<GB_gameboy_t*>(&gb);
        GB_direct_access_t type;
        std::size_t offset;
        if (a < 0x8000) {
            // Literal FF50's implementation returns only boot_rom_finished and
            // does not synchronize a peripheral. General safe reads can sync
            // PPU/APU and change a saved state, so do not use them elsewhere.
            const bool mappedBoot = a < 0x100 && !(GB_safe_read_memory(instance, 0xFF50) & 1);
            type = mappedBoot ? GB_DIRECT_ACCESS_BOOTROM : GB_DIRECT_ACCESS_ROM;
            offset = a;
        }
        else if (a >= 0x8000 && a < 0xA000) { type = GB_DIRECT_ACCESS_VRAM; offset = a - 0x8000; }
        else if (a >= 0xC000 && a < 0xE000) { type = GB_DIRECT_ACCESS_RAM; offset = a - 0xC000; }
        else if (a >= 0xFE00 && a < 0xFEA0) { type = GB_DIRECT_ACCESS_OAM; offset = a - 0xFE00; }
        else if (a >= 0xFF80 && a < 0xFFFF) { type = GB_DIRECT_ACCESS_HRAM; offset = a - 0xFF80; }
        else if (a == 0xFFFF) { type = GB_DIRECT_ACCESS_IE; offset = 0; }
        else if (a >= 0xFF00 && a < 0xFF80) {
            if (a == 0xFF50) return GB_safe_read_memory(instance, 0xFF50);
            type = GB_DIRECT_ACCESS_IO; offset = a - 0xFF00;
        }
        else return 0xFF; // No storage. Snapshot marks this unavailable, not an observed bus value.
        std::size_t size = 0;
        auto* bytes = static_cast<std::uint8_t*>(GB_get_direct_access(instance, type, &size, nullptr));
        return bytes && offset < size ? bytes[offset] : 0xFF;
    }
    static bool write(GB_gameboy_t* instance, std::uint16_t address, std::uint8_t value) {
        auto& s = self(instance);
        int region = address < 0x8000 || (address >= 0xA000 && address < 0xC000) ? 0 :
                     address < 0xA000 ? 1 : address < 0xFE00 ? 2 : address < 0xFF00 ? 3 : 4;
        ++s.activity.writes[region];
        if (s.pendingCount == s.pending.size()) { ++s.evicted; return true; }
        auto& event = s.pending[s.pendingCount++];
        event = {};
        event.id = ++s.eventId;
        event.instructionSerial = s.instructions;
        event.startTicks = s.ticks;
        event.instruction = s.active;
        event.address = address;
        event.canonicalAddress = canonicalAddress(address);
        event.bank = address < 0x8000 ? address / 0x4000 : 0;
        event.physicalStorage = s.physical(address);
        event.valuesAvailable = s.available(address);
        event.before = s.inspect(address);
        event.requested = value;
        return true; // Observation must never suppress a core write.
    }
    StepResult atomic() {
        assertOwner();
        active.reset();
        pendingCount = 0;
        frameArrived = false;
        auto elapsed = GB_run(&gb);
        ticks += elapsed;
        if (frameArrived) frameTick = ticks;
        for (std::size_t j = 0; j < pendingCount; ++j) {
            auto event = pending[j];
            event.endTicks = ticks;
            event.after = inspect(event.address);
            if (history.size() == capacity) { history.pop_front(); ++evicted; }
            history.push_back(event);
        }
        return {active.has_value(), frameArrived, elapsed};
    }
};

Engine::Engine(std::size_t capacity) : impl_(std::make_unique<Impl>(capacity)) { loadTeaching(); }
Engine::~Engine() = default;
void Engine::loadTeaching() { loadRom(demo::rom); }
void Engine::loadRom(std::span<const std::uint8_t> rom) {
    auto& s = *impl_;
    s.assertOwner();
    if (rom.size() != 32768 || rom[0x147] != 0 || (rom[0x143] & 0x80)) {
        throw std::invalid_argument("This slice accepts 32 KiB ROM-only monochrome DMG cartridges. Use the included teaching ROM.");
    }
    // Make a copy before resetting, including when restart() passes the existing vector.
    std::vector<std::uint8_t> copy(rom.begin(), rom.end());
    s.teaching = std::equal(copy.begin(), copy.end(), demo::rom.begin());
    if (s.inited) GB_free(&s.gb);
    // Stable startup noise for reproducible lessons; not a claim about power-on RAM.
    GB_random_seed(0x434F4E534F4C45ull);
    GB_init(&s.gb, GB_MODEL_DMG_B);
    s.inited = true;
    s.rom = std::move(copy);
    GB_set_user_data(&s.gb, &s);
    GB_set_rgb_encode_callback(&s.gb, Impl::encode);
    GB_set_palette(&s.gb, &GB_PALETTE_DMG);
    s.rendering.fill(0xFFCADC9Fu);
    s.completed = s.rendering;
    GB_set_pixels_output(&s.gb, s.rendering.data());
    GB_set_vblank_callback(&s.gb, Impl::vblank);
    GB_set_execution_callback(&s.gb, Impl::execution);
    GB_set_write_memory_callback(&s.gb, s.traceEnabled ? Impl::write : nullptr);
    GB_set_emulate_joypad_bouncing(&s.gb, false);
    GB_set_turbo_mode(&s.gb, true, true);
    GB_load_boot_rom_from_buffer(&s.gb, demo::boot.data(), demo::boot.size());
    GB_load_rom_from_buffer(&s.gb, s.rom.data(), s.rom.size());
    s.ticks = s.instructions = s.frames = s.frameTick = s.eventId = s.evicted = 0;
    s.history.clear(); s.active.reset(); s.lastExecuted.reset(); s.activity = {};
    s.frameKind = "No completed frame";
}
void Engine::restart() { loadRom(impl_->rom); }
void Engine::setButton(Button button, bool down) {
    impl_->assertOwner();
    GB_set_key_state(&impl_->gb, static_cast<GB_key_t>(button), down);
}
void Engine::releaseButtons() {
    for (int i = 0; i < 8; ++i) setButton(static_cast<Button>(i), false);
}
void Engine::setTraceEnabled(bool enabled) {
    auto& s = *impl_; s.assertOwner();
    if (s.traceEnabled == enabled) return;
    s.traceEnabled = enabled;
    GB_set_write_memory_callback(&s.gb, enabled ? Impl::write : nullptr);
    // Old writers could be stale after an unobserved interval. Clear the capture.
    s.history.clear(); s.evicted = 0; s.activity = {}; s.activity.startTicks = s.ticks;
}
StepResult Engine::stepInstruction() {
    auto start = ticks();
    for (unsigned i = 0; i < 20000; ++i) {
        auto result = impl_->atomic();
        if (result.executedInstruction) return {true, result.completedFrame, ticks() - start};
        if (ticks() - start >= 2 * 70224 * 2) break;
    }
    return {false, false, ticks() - start}; // HALT/STOP has no next opcode yet.
}
StepResult Engine::stepFrame() {
    auto start = ticks();
    for (unsigned i = 0; i < 100000; ++i) {
        auto result = impl_->atomic();
        if (result.completedFrame) return {result.executedInstruction, true, ticks() - start};
        if (ticks() - start > 4 * 70224 * 2) break;
    }
    return {false, false, ticks() - start};
}
void Engine::advanceTo(std::uint64_t targetTicks, std::chrono::microseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    unsigned count = 0;
    while (ticks() < targetTicks) {
        impl_->atomic();
        if ((++count % 32 == 0) && std::chrono::steady_clock::now() >= deadline) break;
    }
}
std::uint8_t Engine::inspect(std::uint16_t address) const {
    impl_->assertOwner(); return impl_->inspect(address);
}
Snapshot Engine::snapshot(std::uint16_t memoryBase) {
    auto& s = *impl_; s.assertOwner();
    Snapshot out;
    auto* r = GB_get_registers(&s.gb);
    out.registers = {r->af, r->bc, r->de, r->hl, r->sp, r->pc};
    out.next = s.instruction(r->pc);
    out.lastExecuted = s.lastExecuted;
    out.ticks = s.ticks; out.instructions = s.instructions; out.frames = s.frames;
    out.frameBoundaryTicks = s.frameTick; out.frameKind = s.frameKind;
    out.evictedWrites = s.evicted;
    out.oldestRetainedTick = s.history.empty() ? s.ticks : s.history.front().startTicks;
    out.teaching = s.teaching; out.traceEnabled = s.traceEnabled;
    out.memoryBase = std::min<std::uint16_t>(memoryBase, 0xFF80);
    for (unsigned i = 0; i < out.memory.size(); ++i) {
        out.memory[i] = s.inspect(out.memoryBase + i);
        out.memoryAvailable[i] = s.available(out.memoryBase + i);
    }
    for (unsigned i = 0; i < out.sprite.size(); ++i) out.sprite[i] = s.inspect(0xFE00 + i);
    out.playerX = s.inspect(demo::player_x); out.playerY = s.inspect(demo::player_y);
    out.buttons = s.inspect(demo::buttons); out.ly = s.inspect(0xFF44); out.lcdc = s.inspect(0xFF40);
    out.pixels = s.completed;
    out.writes.assign(s.history.begin(), s.history.end());
    out.activity = s.activity; out.activity.endTicks = s.ticks;
    s.activity = {}; s.activity.startTicks = s.ticks;
    return out;
}
std::vector<std::uint8_t> Engine::stateBytes() const {
    impl_->assertOwner();
    auto* instance = &impl_->gb;
    std::vector<std::uint8_t> state(GB_get_save_state_size(instance));
    GB_save_state_to_buffer(instance, state.data());
    return state;
}
std::uint64_t Engine::ticks() const { impl_->assertOwner(); return impl_->ticks; }
std::size_t Engine::traceSize() const { impl_->assertOwner(); return impl_->history.size(); }
std::size_t Engine::traceCapacity() const { return impl_->capacity; }
} // namespace observatory
