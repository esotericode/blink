#pragma once
#include "emulator/cartridge.hpp"
#include "emulator/graphics.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace observatory {
inline constexpr std::size_t screenPixels = 160 * 144;
inline constexpr std::size_t memoryWindow = 128;
inline constexpr std::uint64_t ticksPerSecond = 8388608;

struct Registers {
    std::uint16_t af{}, bc{}, de{}, hl{}, sp{}, pc{};
    bool operator==(const Registers&) const = default;
};
struct Instruction {
    std::uint16_t pc{};
    std::array<std::uint8_t, 3> bytes{};
    bool available = true;
    std::uint16_t bank{}; // ROM bank mapped at pc ($0000-$7FFF); 0 elsewhere
    bool operator==(const Instruction&) const = default;
};
// Cartridge banks the CPU can see: rom0 at $0000-$3FFF, rom at $4000-$7FFF,
// ram at $A000-$BFFF (reported by SameBoy's direct-access API).
struct BankMapping {
    std::uint16_t rom0{}, rom{1}, ram{};
    bool operator==(const BankMapping&) const = default;
};
struct WriteEvent {
    std::uint64_t id{}, instructionSerial{}, startTicks{}, endTicks{};
    std::optional<Instruction> instruction;
    std::uint16_t address{}, canonicalAddress{}, bank{}; // bank of the addressed ROM/RAM window
    // Mapping when the attempt was made and at the end of its step. On banked
    // cartridges a write to $0000-$7FFF is an MBC command; these show its effect.
    BankMapping banksBefore, banksAfter;
    std::uint8_t before{}, requested{}, after{};
    // Physical storage is available for VRAM, WRAM (including echo), OAM, HRAM.
    // Else before/after are raw register or ROM storage, not CPU bus values.
    bool physicalStorage{};
    bool valuesAvailable = true;
};
struct Activity {
    std::uint64_t startTicks{}, endTicks{}, instructions{};
    std::array<std::uint64_t, 5> writes{}; // ROM/cart, VRAM, WRAM, OAM, IO/HRAM
};
// Cartridge header facts plus the live mapping at the shared cursor.
struct CartridgeState {
    CartridgeInfo info;
    BankMapping banks;
    std::size_t romBytes{}, ramBytes{}; // emulator buffers (ROM rounded up to a power of two)
    bool bootMapped{};                  // boot program still visible at $0000-$00FF
};
// Raw video storage copied at the same instruction boundary as the CPU state.
struct VideoState {
    Vram vram{};
    Oam oam{};
    std::uint8_t lcdc{}, stat{}, scy{}, scx{}, ly{}, lyc{}, bgp{}, obp0{}, obp1{}, wy{}, wx{};
};
struct Snapshot {
    std::uint64_t ticks{}, instructions{}, frames{}, frameBoundaryTicks{};
    std::uint64_t evictedWrites{}, oldestRetainedTick{};
    Registers registers;
    Instruction next;
    std::optional<Instruction> lastExecuted;
    std::uint16_t memoryBase{};
    std::array<std::uint8_t, memoryWindow> memory{};
    std::array<bool, memoryWindow> memoryAvailable{};
    VideoState video;
    CartridgeState cartridge;
    std::uint8_t playerX{}, playerY{}, buttons{};
    std::uint8_t heldButtons{}; // Bit n set while Button(n) is held by the host.
    bool teaching{}, bankDemo{}, traceEnabled{}; // exact bundled-ROM identity, not a filename
    std::string frameKind;
    // Latest completed output, tagged separately from CPU/memory cursor.
    // Frames live on the heap: inline they made each Snapshot ~190 KB, and
    // Windows threads get 1-2 MiB stacks. Always screenPixels long.
    std::vector<std::uint32_t> pixels = std::vector<std::uint32_t>(screenPixels);
    // The output completed immediately before `pixels` (number previousFrame;
    // 0 when no earlier output exists since reset).
    std::vector<std::uint32_t> previousPixels = std::vector<std::uint32_t>(screenPixels);
    std::uint64_t previousFrame{};
    std::vector<WriteEvent> writes;
    Activity activity;
};
// Per-address counts over [startTicks, endTicks]. Writes are CPU write attempts by
// bus address (only while capture is on); executions count opcode starts by PC.
struct ActivityMap {
    std::uint64_t startTicks{}, endTicks{};
    bool writesObserved{};
    std::vector<std::uint32_t> writes, executions;
    // Opcode starts per ROM bank (index = bank), and boot-program opcodes.
    std::vector<std::uint32_t> bankExecutions;
    std::uint64_t bootExecutions{};
    // Atomic steps that ended with a different mapping than they began with.
    std::uint64_t romBankChanges{}, ramBankChanges{};
};
struct WatchResult {
    enum class Stop { Write, BankChange, Limit, CaptureOff };
    Stop stop = Stop::Limit;
    std::optional<WriteEvent> write; // First matching attempt in the stopping step.
    std::uint64_t advancedTicks{}, instructions{}, frames{};
    BankMapping banksBefore, banksAfter; // For BankChange: the mapping across the stopping step.
};
enum class Button { Right, Left, Up, Down, A, B, Select, Start };
struct StepResult {
    bool executedInstruction{};
    bool completedFrame{};
    std::uint64_t advancedTicks{};
};
std::uint16_t canonicalAddress(std::uint16_t address);
std::string hex(std::uint64_t value, int width = 4);
std::string disassemble(const Instruction& instruction);
int instructionLength(std::uint8_t opcode);
} // namespace observatory
