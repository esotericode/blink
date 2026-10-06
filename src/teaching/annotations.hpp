#pragma once
#include "emulator/state.hpp"
#include <cstdint>
#include <string>
namespace observatory {
// Bundled original programs, recognised by exact ROM bytes (never by filename).
// Only these get game-variable names and source notes.
enum class Program { Other, Teaching, BankDemo };
Program programOf(const Snapshot& snapshot);
std::string addressName(std::uint16_t address, Program program);
// Note for the instruction at pc; bank matters for code at $4000-$7FFF.
std::string instructionNote(std::uint16_t pc, std::uint16_t bank, Program program);
// DMG memory-map region for an address (Pan Docs "Memory Map"). The mapped
// bank is live state; panels add it from the snapshot.
std::string regionName(std::uint16_t address);
}
