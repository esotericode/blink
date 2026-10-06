#pragma once
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
    bool operator==(const Instruction&) const = default;
};
struct WriteEvent {
    std::uint64_t id{}, instructionSerial{}, startTicks{}, endTicks{};
    std::optional<Instruction> instruction;
    std::uint16_t address{}, canonicalAddress{}, bank{};
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
    std::uint8_t playerX{}, playerY{}, buttons{};
    std::uint8_t heldButtons{}; // Bit n set while Button(n) is held by the host.
    bool teaching{}, traceEnabled{};
    std::string frameKind;
    // Latest completed output, tagged separately from CPU/memory cursor.
    std::array<std::uint32_t, screenPixels> pixels{};
    // The output completed immediately before `pixels` (number previousFrame;
    // 0 when no earlier output exists since reset).
    std::array<std::uint32_t, screenPixels> previousPixels{};
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
};
struct WatchResult {
    enum class Stop { Write, Limit, CaptureOff };
    Stop stop = Stop::Limit;
    std::optional<WriteEvent> write; // First matching attempt in the stopping step.
    std::uint64_t advancedTicks{}, instructions{}, frames{};
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
