#pragma once
#include <cstdint>
#include <string>
namespace observatory {
std::string addressName(std::uint16_t address, bool teaching);
std::string instructionNote(std::uint16_t pc, bool teaching);
}
