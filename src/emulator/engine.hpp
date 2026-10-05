#pragma once
#include "emulator/state.hpp"
#include <chrono>
#include <memory>
#include <span>

namespace observatory {
// One owner, no Qt dependency. Every method must be called on the owning thread.
// UI copies Snapshot only after GB_run returns. No inspector may drive CPU reads.
class Engine {
public:
    explicit Engine(std::size_t traceCapacity = 4096);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    void loadTeaching();
    void loadRom(std::span<const std::uint8_t> rom);
    void setButton(Button button, bool down);
    void releaseButtons();
    void setTraceEnabled(bool enabled);
    StepResult stepInstruction();
    StepResult stepFrame();
    // Bounded UI quantum; stop at an instruction boundary when time budget ends.
    void advanceTo(std::uint64_t targetTicks, std::chrono::microseconds budget);
    Snapshot snapshot(std::uint16_t memoryBase = 0xC000);
    std::uint8_t inspect(std::uint16_t address) const;
    std::vector<std::uint8_t> stateBytes() const;
    std::uint64_t ticks() const;
    std::size_t traceSize() const;
    std::size_t traceCapacity() const;
    void restart();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace observatory
