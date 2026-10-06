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
    std::array<std::uint32_t, screenPixels> rendering{}, completed{}, previous{};
    std::uint64_t ticks{}, instructions{}, frames{}, frameTick{}, eventId{}, evicted{}, previousFrame{};
    std::string frameKind = "No completed frame";
    bool traceEnabled = true, teaching{}, bankDemo{}, frameArrived{}, inited{}, bootDone{}, carriedBatteryDirty{};
    CartridgeInfo cartridge;
    BankMapping mapping;               // Mapping at the start of the current atomic step.
    std::vector<std::uint32_t> bankExecutions;
    std::uint64_t bootExecutions{}, romBankChanges{}, ramBankChanges{};
    std::optional<Instruction> active, lastExecuted;
    std::deque<WriteEvent> history;
    // OAM DMA: at most one copy runs; finished records are bounded.
    static constexpr std::size_t dmaCapacity = 64;
    // SameBoy: one warm-up machine cycle, 160 copies, one closing cycle (4
    // T-cycles each, 2 ticks per T-cycle). Its DMA pauses while the CPU is halted.
    static constexpr std::uint64_t dmaTicks = 162 * 4 * 2;
    std::optional<DmaTransfer> dmaPending;
    std::uint64_t dmaDeadline{}, dmaId{};
    std::deque<DmaTransfer> dmaHistory;
    bool stepDma{};
    std::array<WriteEvent, 8> pending{};
    std::size_t pendingCount{}, stepWrites{};
    Activity activity;
    std::vector<std::uint32_t> writeCounts = std::vector<std::uint32_t>(0x10000);
    std::vector<std::uint32_t> executionCounts = std::vector<std::uint32_t>(0x10000);
    std::uint64_t mapStart{};
    std::uint8_t held{};

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
        s.previous = s.completed;
        s.previousFrame = s.frames;
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
    // Boot mapping is one-way: once FF50 unmaps it, it never returns.
    bool bootMapped() const {
        // Literal FF50's implementation returns only boot_rom_finished and does
        // not synchronize a peripheral. General safe reads can sync PPU/APU and
        // change a saved state, so they are not used anywhere else.
        return !bootDone && !(GB_safe_read_memory(const_cast<GB_gameboy_t*>(&gb), 0xFF50) & 1);
    }
    std::uint16_t bankOf(GB_direct_access_t type, std::size_t* size = nullptr) const {
        std::size_t bytes = 0; std::uint16_t bank = 0;
        GB_get_direct_access(const_cast<GB_gameboy_t*>(&gb), type, &bytes, &bank);
        if (size) *size = bytes;
        return bank;
    }
    BankMapping banks() const {
        std::size_t ramSize = 0;
        BankMapping m;
        m.rom0 = bankOf(GB_DIRECT_ACCESS_ROM0);
        m.rom = bankOf(GB_DIRECT_ACCESS_ROM);
        const auto ram = bankOf(GB_DIRECT_ACCESS_CART_RAM, &ramSize);
        m.ram = ramSize ? ram : 0;
        return m;
    }
    std::uint16_t romBankAt(std::uint16_t pc) const {
        if (pc >= 0x8000 || (pc < 0x100 && bootMapped())) return 0;
        return pc < 0x4000 ? bankOf(GB_DIRECT_ACCESS_ROM0) : bankOf(GB_DIRECT_ACCESS_ROM);
    }
    Instruction instruction(std::uint16_t pc) const {
        Instruction i{pc, {}};
        i.bank = romBankAt(pc);
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
        ++s.executionCounts[pc];
        auto i = s.instruction(pc);
        if (pc < 0x100 && s.bootMapped()) ++s.bootExecutions;
        else if (pc < 0x8000 && i.bank < s.bankExecutions.size()) ++s.bankExecutions[i.bank];
        i.bytes[0] = opcode; // Actual fetched opcode; operands are storage observations.
        s.active = i;
        s.lastExecuted = i;
    }
    std::size_t cartRamBytes() const {
        std::size_t size = 0;
        bankOf(GB_DIRECT_ACCESS_CART_RAM, &size);
        return size;
    }
    bool physical(std::uint16_t address) const {
        auto a = canonicalAddress(address);
        return (a >= 0x8000 && a < 0xA000) || (a >= 0xC000 && a < 0xE000) ||
               (a >= 0xFE00 && a < 0xFEA0) || (a >= 0xFF80 && a < 0xFFFF) ||
               (a >= 0xA000 && a < 0xC000 && cartRamBytes());
    }
    bool available(std::uint16_t address) const {
        auto a = canonicalAddress(address);
        return a < 0xA000 || (a >= 0xC000 && a < 0xE000) ||
               (a >= 0xFE00 && a < 0xFEA0) || a >= 0xFF00 ||
               (a >= 0xA000 && a < 0xC000 && cartRamBytes());
    }
    std::uint8_t inspect(std::uint16_t address) const {
        auto a = canonicalAddress(address);
        auto* instance = const_cast<GB_gameboy_t*>(&gb);
        GB_direct_access_t type;
        std::size_t offset;
        if (a < 0x8000) {
            if (a < 0x100 && bootMapped()) { type = GB_DIRECT_ACCESS_BOOTROM; offset = a; }
            else {
                // The bank SameBoy maps into each 16 KiB window, as the CPU would see it.
                type = a < 0x4000 ? GB_DIRECT_ACCESS_ROM0 : GB_DIRECT_ACCESS_ROM;
                offset = std::size_t(bankOf(type)) * 0x4000 + (a & 0x3FFF);
            }
        }
        else if (a >= 0xA000 && a < 0xC000) {
            // Storage of the selected RAM bank, indexed like SameBoy's read path.
            // Whether the CPU currently sees it depends on the MBC's RAM enable.
            std::size_t size = 0;
            const auto bank = bankOf(GB_DIRECT_ACCESS_CART_RAM, &size);
            if (!size) return 0xFF;
            type = GB_DIRECT_ACCESS_CART_RAM;
            offset = ((a & 0x1FFF) + std::size_t(bank) * 0x2000) & (size - 1);
        }
        else if (a >= 0x8000 && a < 0xA000) { type = GB_DIRECT_ACCESS_VRAM; offset = a - 0x8000; }
        else if (a >= 0xC000 && a < 0xE000) { type = GB_DIRECT_ACCESS_RAM; offset = a - 0xC000; }
        else if (a >= 0xFE00 && a < 0xFEA0) { type = GB_DIRECT_ACCESS_OAM; offset = a - 0xFE00; }
        else if (a >= 0xFF80 && a < 0xFFFF) { type = GB_DIRECT_ACCESS_HRAM; offset = a - 0xFF80; }
        else if (a == 0xFFFF) { type = GB_DIRECT_ACCESS_IE; offset = 0; }
        else if (a >= 0xFF00 && a < 0xFF80) {
            if (a == 0xFF50) return GB_safe_read_memory(instance, 0xFF50); // see bootMapped()
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
        ++s.writeCounts[address];
        if (address == 0xFF46) s.requestDma(value);
        if (s.pendingCount == s.pending.size()) { ++s.evicted; return true; }
        auto& event = s.pending[s.pendingCount++];
        event = {};
        event.id = ++s.eventId;
        event.instructionSerial = s.instructions;
        event.startTicks = s.ticks;
        event.instruction = s.active;
        event.address = address;
        event.canonicalAddress = canonicalAddress(address);
        event.banksBefore = s.mapping;
        event.bank = address < 0x4000 ? s.mapping.rom0 : address < 0x8000 ? s.mapping.rom :
                     (address >= 0xA000 && address < 0xC000) ? s.mapping.ram : 0;
        event.physicalStorage = s.physical(address);
        event.valuesAvailable = s.available(address);
        event.before = s.inspect(address);
        event.requested = value;
        return true; // Observation must never suppress a core write.
    }
    void copyOam(std::array<std::uint8_t, oamBytes>& out) const {
        std::size_t size = 0;
        auto* bytes = static_cast<const std::uint8_t*>(GB_get_direct_access(const_cast<GB_gameboy_t*>(&gb), GB_DIRECT_ACCESS_OAM, &size, nullptr));
        if (bytes) std::copy_n(bytes, std::min(size, out.size()), out.begin());
    }
    // Called from the write hook before the core accepts the $FF46 write, so
    // OAM is still the "before" state. A copy already running restarts.
    void requestDma(std::uint8_t page) {
        if (dmaPending) finishDma(DmaTransfer::Status::Restarted);
        DmaTransfer t;
        t.id = ++dmaId;
        t.requestStartTicks = ticks;
        t.instruction = active;
        t.page = page;
        copyOam(t.before);
        dmaPending = t;
    }
    void finishDma(DmaTransfer::Status status) {
        auto t = *dmaPending;
        dmaPending.reset();
        if (!t.requestEndTicks) t.requestEndTicks = ticks;
        t.status = status;
        t.checkedTicks = ticks;
        copyOam(t.after);
        t.matching = 0;
        for (std::size_t i = 0; i < oamBytes; ++i) t.matching += t.after[i] == inspect(t.sourceOf(i));
        if (dmaHistory.size() == dmaCapacity) dmaHistory.pop_front();
        dmaHistory.push_back(t);
        ++activity.dmaTransfers;
        stepDma = status == DmaTransfer::Status::Checked; // a restarted copy is superseded
    }
    void clearMap() {
        std::fill(writeCounts.begin(), writeCounts.end(), 0);
        std::fill(executionCounts.begin(), executionCounts.end(), 0);
        std::fill(bankExecutions.begin(), bankExecutions.end(), 0);
        bootExecutions = romBankChanges = ramBankChanges = 0;
        mapStart = ticks;
    }
    StepResult atomic() {
        assertOwner();
        active.reset();
        pendingCount = 0;
        frameArrived = false;
        stepDma = false;
        auto elapsed = GB_run(&gb);
        ticks += elapsed;
        if (dmaPending) {
            if (!dmaPending->requestEndTicks) {
                // The write happened somewhere in this step; count from its end.
                dmaPending->requestEndTicks = ticks;
                dmaDeadline = ticks + dmaTicks;
            } else if (!active) {
                dmaDeadline += elapsed; // halted (DMA paused) or interrupt service; checking late is safe
            }
            if (ticks >= dmaDeadline) finishDma(DmaTransfer::Status::Checked);
        }
        if (frameArrived) frameTick = ticks;
        if (!bootDone && !bootMapped()) bootDone = true;
        const auto after = banks();
        romBankChanges += after.rom != mapping.rom || after.rom0 != mapping.rom0;
        ramBankChanges += after.ram != mapping.ram;
        for (std::size_t j = 0; j < pendingCount; ++j) {
            auto event = pending[j];
            event.endTicks = ticks;
            event.banksAfter = after;
            event.after = inspect(event.address);
            if (history.size() == capacity) { history.pop_front(); ++evicted; }
            history.push_back(event);
        }
        stepWrites = pendingCount;
        mapping = after;
        return {active.has_value(), frameArrived, elapsed};
    }
};

Engine::Engine(std::size_t capacity) : impl_(std::make_unique<Impl>(capacity)) { loadTeaching(); }
Engine::~Engine() = default;
void Engine::loadTeaching() { loadRom(demo::rom); }
void Engine::loadRom(std::span<const std::uint8_t> rom) {
    auto& s = *impl_;
    s.assertOwner();
    if (rom.size() < 0x150 || rom.size() > maxRomBytes) {
        throw std::invalid_argument("Not a Game Boy ROM: a cartridge image holds a header at $0100-$014F and is at most 8 MiB.");
    }
    const auto cartridge = describeCartridge(rom);
    if (!cartridge.supported) {
        throw std::invalid_argument("The SameBoy core does not emulate this cartridge controller.");
    }
    // Make a copy before resetting, including when restart() passes the existing vector.
    std::vector<std::uint8_t> copy(rom.begin(), rom.end());
    auto same = [&](const auto& bundled) { return copy.size() == bundled.size() && std::equal(copy.begin(), copy.end(), bundled.begin()); };
    s.teaching = same(demo::rom);
    s.bankDemo = same(bankdemo::rom);
    s.cartridge = cartridge;
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
    s.completed = s.previous = s.rendering;
    GB_set_pixels_output(&s.gb, s.rendering.data());
    GB_set_vblank_callback(&s.gb, Impl::vblank);
    GB_set_execution_callback(&s.gb, Impl::execution);
    GB_set_write_memory_callback(&s.gb, s.traceEnabled ? Impl::write : nullptr);
    GB_set_emulate_joypad_bouncing(&s.gb, false);
    GB_set_turbo_mode(&s.gb, true, true);
    GB_load_boot_rom_from_buffer(&s.gb, demo::boot.data(), demo::boot.size());
    GB_load_rom_from_buffer(&s.gb, s.rom.data(), s.rom.size());
    s.ticks = s.instructions = s.frames = s.frameTick = s.eventId = s.evicted = s.previousFrame = 0;
    std::size_t romSize = 0;
    s.bankOf(GB_DIRECT_ACCESS_ROM, &romSize);
    s.bankExecutions.assign(romSize / 0x4000, 0);
    s.bootDone = false;
    s.mapping = s.banks();
    s.history.clear(); s.active.reset(); s.lastExecuted.reset(); s.activity = {};
    s.dmaHistory.clear(); s.dmaPending.reset(); s.dmaId = 0;
    s.held = 0; s.clearMap();
    s.carriedBatteryDirty = false;
    s.frameKind = "No completed frame";
}
void Engine::restart() {
    // Like switching a Game Boy off and on: the battery keeps cartridge RAM.
    const auto battery = batteryData();
    const bool dirty = batteryDirty();
    loadRom(impl_->rom);
    if (!battery.empty()) loadBattery(battery);
    impl_->carriedBatteryDirty = dirty && !battery.empty();
}
std::vector<std::uint8_t> Engine::batteryData() const {
    auto& s = *impl_; s.assertOwner();
    const int size = GB_save_battery_size(const_cast<GB_gameboy_t*>(&s.gb));
    if (size <= 0) return {};
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    GB_save_battery_to_buffer(const_cast<GB_gameboy_t*>(&s.gb), data.data(), data.size());
    return data;
}
bool Engine::loadBattery(std::span<const std::uint8_t> data) {
    auto& s = *impl_; s.assertOwner();
    const auto ram = s.cartRamBytes();
    const int expected = GB_save_battery_size(&s.gb);
    if (expected <= 0 || data.empty() || data.size() < ram) return false;
    const auto footer = data.size() - ram;
    const auto nativeFooter = std::size_t(expected) - ram;
    // These are the formats read by the pinned Core/gb.c loader. Its generic
    // RTC union copy uses total size, not footer size, so legacy inputs need
    // backing storage for the full 48-byte union while retaining logical size.
    const bool genericClock = s.cartridge.timer && s.cartridge.mbc != Mbc::Huc3 && s.cartridge.mbc != Mbc::Tpp1;
    const auto legacyFooter = sizeof(GB_rtc_time_t) + sizeof(time_t);
    if (footer != 0 && footer != nativeFooter && !(genericClock && (footer == legacyFooter || footer == 44))) return false;
    if (!s.cartridge.timer && footer != 0) return false;
    if (genericClock && footer != 0 && footer < 48) {
        std::vector<std::uint8_t> padded(ram + 48, 0);
        std::copy(data.begin(), data.end(), padded.begin());
        GB_load_battery_from_buffer(&s.gb, padded.data(), data.size());
    } else {
        GB_load_battery_from_buffer(&s.gb, data.data(), data.size());
    }
    GB_clear_battery_dirty(&s.gb);
    s.carriedBatteryDirty = false;
    return true;
}
bool Engine::batteryDirty() const { impl_->assertOwner(); return impl_->carriedBatteryDirty || GB_get_battery_dirty(const_cast<GB_gameboy_t*>(&impl_->gb)); }
void Engine::clearBatteryDirty() { impl_->assertOwner(); GB_clear_battery_dirty(&impl_->gb); impl_->carriedBatteryDirty = false; }
void Engine::setButton(Button button, bool down) {
    impl_->assertOwner();
    GB_set_key_state(&impl_->gb, static_cast<GB_key_t>(button), down);
    const auto bit = std::uint8_t(1u << static_cast<unsigned>(button));
    impl_->held = down ? impl_->held | bit : impl_->held & ~bit;
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
    s.dmaHistory.clear(); s.dmaPending.reset();
    s.clearMap();
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
WatchResult Engine::runUntilWrite(std::uint16_t address, std::uint64_t limitTicks) {
    return runUntilWrite(address, address, limitTicks);
}
WatchResult Engine::runUntilWrite(std::uint16_t first, std::uint16_t last, std::uint64_t limitTicks) {
    auto& s = *impl_; s.assertOwner();
    WatchResult result;
    if (!s.traceEnabled) { result.stop = WatchResult::Stop::CaptureOff; return result; }
    const auto low = canonicalAddress(first), high = canonicalAddress(last);
    const auto ticks0 = s.ticks, instructions0 = s.instructions, frames0 = s.frames;
    result.banksBefore = s.mapping;
    // OAM is also written by DMA: stop once a copy into the range has been checked.
    const bool oam = low <= 0xFE9F && high >= 0xFE00;
    for (unsigned calls = 0; calls < 4000000 && s.ticks - ticks0 < limitTicks && !result.write && !result.dma; ++calls) {
        s.atomic();
        // This step's records are the newest; capacity (>= 8) always retains them.
        for (auto i = s.history.size() - std::min(s.stepWrites, s.history.size()); i < s.history.size(); ++i) {
            const auto a = s.history[i].canonicalAddress;
            if (a >= low && a <= high) { result.write = s.history[i]; break; }
        }
        if (!result.write && oam && s.stepDma) result.dma = s.dmaHistory.back();
    }
    result.stop = result.write ? WatchResult::Stop::Write : result.dma ? WatchResult::Stop::Dma : WatchResult::Stop::Limit;
    result.banksAfter = s.mapping;
    result.advancedTicks = s.ticks - ticks0;
    result.instructions = s.instructions - instructions0;
    result.frames = s.frames - frames0;
    return result;
}
WatchResult Engine::runUntilBankChange(std::uint64_t limitTicks) {
    auto& s = *impl_; s.assertOwner();
    WatchResult result;
    const auto ticks0 = s.ticks, instructions0 = s.instructions, frames0 = s.frames;
    bool changed = false;
    for (unsigned calls = 0; calls < 4000000 && s.ticks - ticks0 < limitTicks && !changed; ++calls) {
        result.banksBefore = s.mapping;
        s.atomic();
        changed = s.mapping != result.banksBefore;
    }
    result.banksAfter = s.mapping;
    result.stop = changed ? WatchResult::Stop::BankChange : WatchResult::Stop::Limit;
    if (changed && s.traceEnabled) {
        // The controller write that caused it, when capture saw this step.
        for (auto i = s.history.size() - std::min(s.stepWrites, s.history.size()); i < s.history.size(); ++i) {
            if (s.history[i].address < 0x8000) result.write = s.history[i];
        }
    }
    result.advancedTicks = s.ticks - ticks0;
    result.instructions = s.instructions - instructions0;
    result.frames = s.frames - frames0;
    return result;
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
    out.teaching = s.teaching; out.bankDemo = s.bankDemo; out.traceEnabled = s.traceEnabled;
    out.cartridge.info = s.cartridge;
    out.cartridge.banks = s.mapping;
    s.bankOf(GB_DIRECT_ACCESS_ROM, &out.cartridge.romBytes);
    out.cartridge.ramBytes = s.cartRamBytes();
    out.cartridge.bootMapped = s.bootMapped();
    out.memoryBase = std::min<std::uint16_t>(memoryBase, 0xFF80);
    for (unsigned i = 0; i < out.memory.size(); ++i) {
        out.memory[i] = s.inspect(out.memoryBase + i);
        out.memoryAvailable[i] = s.available(out.memoryBase + i);
    }
    auto copyStorage = [&](GB_direct_access_t type, auto& destination) {
        std::size_t size = 0;
        auto* bytes = static_cast<const std::uint8_t*>(GB_get_direct_access(&s.gb, type, &size, nullptr));
        if (bytes) std::copy_n(bytes, std::min(size, destination.size()), destination.begin());
    };
    copyStorage(GB_DIRECT_ACCESS_VRAM, out.video.vram);
    copyStorage(GB_DIRECT_ACCESS_OAM, out.video.oam);
    auto& v = out.video;
    v.lcdc = s.inspect(0xFF40); v.stat = s.inspect(0xFF41); v.scy = s.inspect(0xFF42); v.scx = s.inspect(0xFF43);
    v.ly = s.inspect(0xFF44); v.lyc = s.inspect(0xFF45); v.bgp = s.inspect(0xFF47); v.obp0 = s.inspect(0xFF48);
    v.obp1 = s.inspect(0xFF49); v.wy = s.inspect(0xFF4A); v.wx = s.inspect(0xFF4B);
    out.playerX = s.inspect(demo::player_x); out.playerY = s.inspect(demo::player_y);
    out.buttons = s.inspect(demo::buttons); out.heldButtons = s.held;
    out.pixels.assign(s.completed.begin(), s.completed.end());
    out.previousPixels.assign(s.previous.begin(), s.previous.end()); out.previousFrame = s.previousFrame;
    out.writes.assign(s.history.begin(), s.history.end());
    out.dma.assign(s.dmaHistory.begin(), s.dmaHistory.end());
    out.dmaCopying = s.dmaPending;
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
void Engine::activityMap(ActivityMap& out) const {
    const auto& s = *impl_; s.assertOwner();
    out.startTicks = s.mapStart; out.endTicks = s.ticks; out.writesObserved = s.traceEnabled;
    out.writes = s.writeCounts; out.executions = s.executionCounts;
    out.bankExecutions = s.bankExecutions; out.bootExecutions = s.bootExecutions;
    out.romBankChanges = s.romBankChanges; out.ramBankChanges = s.ramBankChanges;
}
void Engine::clearActivityMap() { impl_->assertOwner(); impl_->clearMap(); }
std::uint8_t Engine::heldButtons() const { impl_->assertOwner(); return impl_->held; }
std::uint64_t Engine::ticks() const { impl_->assertOwner(); return impl_->ticks; }
std::size_t Engine::traceSize() const { impl_->assertOwner(); return impl_->history.size(); }
std::size_t Engine::traceCapacity() const { return impl_->capacity; }
} // namespace observatory
