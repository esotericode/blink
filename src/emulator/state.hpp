#pragma once
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
struct Snapshot {
    std::uint64_t ticks{}, instructions{}, frames{}, frameBoundaryTicks{};
    std::uint64_t evictedWrites{}, oldestRetainedTick{};
    Registers registers;
    Instruction next;
    std::optional<Instruction> lastExecuted;
    std::uint16_t memoryBase{};
    std::array<std::uint8_t, memoryWindow> memory{};
    std::array<bool, memoryWindow> memoryAvailable{};
    std::array<std::uint8_t, 4> sprite{};
    std::uint8_t playerX{}, playerY{}, buttons{}, ly{}, lcdc{};
    bool teaching{}, traceEnabled{};
    std::string frameKind;
    // Latest completed output, tagged separately from CPU/memory cursor.
    std::array<std::uint32_t, screenPixels> pixels{};
    std::vector<WriteEvent> writes;
    Activity activity;
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
