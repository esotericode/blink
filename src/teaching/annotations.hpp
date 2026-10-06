#pragma once
#include <cstdint>
#include <string>
namespace observatory {
std::string addressName(std::uint16_t address, bool teaching);
std::string instructionNote(std::uint16_t pc, bool teaching);
// DMG memory-map region for an address (Pan Docs "Memory Map"), as this
// ROM-only adapter maps it.
std::string regionName(std::uint16_t address);
}
