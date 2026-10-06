#pragma once
#include "emulator/state.hpp"
#include <chrono>
#include <memory>
#include <span>

namespace observatory {
inline constexpr std::size_t maxRomBytes = 8 * 1024 * 1024; // largest MBC5 cartridge
// One owner, no Qt dependency. Every method must be called on the owning thread.
// UI copies Snapshot only after GB_run returns. No inspector may drive CPU reads.
class Engine {
public:
    explicit Engine(std::size_t traceCapacity = 4096);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    void loadTeaching();
    // Any DMG cartridge image SameBoy can run (header present, at most 8 MiB).
    // Throws std::invalid_argument without touching the current emulation.
    void loadRom(std::span<const std::uint8_t> rom);
    void setButton(Button button, bool down);
    void releaseButtons();
    void setTraceEnabled(bool enabled);
    StepResult stepInstruction();
    StepResult stepFrame();
    // Run atomic steps until one contains a CPU write attempt to `address` (echo
    // aliases match), then stop at that step's end boundary. Requires capture on.
    WatchResult runUntilWrite(std::uint16_t address, std::uint64_t limitTicks);
    WatchResult runUntilWrite(std::uint16_t first, std::uint16_t last, std::uint64_t limitTicks);
    // Run until a step ends with a different ROM0/ROM/RAM bank mapping. Works with
    // capture off; with capture on, `write` is the controller write in that step.
    WatchResult runUntilBankChange(std::uint64_t limitTicks);
    // Bounded UI quantum; stop at an instruction boundary when time budget ends.
    void advanceTo(std::uint64_t targetTicks, std::chrono::microseconds budget);
    Snapshot snapshot(std::uint16_t memoryBase = 0xC000);
    std::uint8_t inspect(std::uint16_t address) const;
    // Copies per-address counts since the last clear (load, restart, capture toggle).
    void activityMap(ActivityMap& out) const;
    void clearActivityMap();
    std::uint8_t heldButtons() const;
    std::vector<std::uint8_t> stateBytes() const;
    std::uint64_t ticks() const;
    std::size_t traceSize() const;
    std::size_t traceCapacity() const;
    // Power cycle: reload the same cartridge, keeping battery-backed RAM.
    void restart();
    // Battery-backed cartridge RAM (and clock) in SameBoy's .sav format; empty
    // when the cartridge has no battery.
    std::vector<std::uint8_t> batteryData() const;
    // Validate before changing RAM. Accept complete RAM and recognized clock
    // footers; false leaves the current state and dirty flag untouched.
    bool loadBattery(std::span<const std::uint8_t> data);
    bool batteryDirty() const;
    void clearBatteryDirty();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace observatory
