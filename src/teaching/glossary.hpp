#pragma once
#include <cstdint>
#include <string>

// Plain-language explanations for everything the interface shows, written for
// a curious newcomer: what the thing is, why it matters, and how to use it.
// Hardware facts follow Pan Docs; nothing here describes a specific game.
namespace observatory {
struct Explanation {
    std::string title, body, hint;
    bool empty() const { return title.empty() && body.empty(); }
};
// Entry by key, e.g. "reg.pc", "flag.z", "part.ppu", "panel.memory",
// "action.run". Unknown keys return an empty explanation.
const Explanation& glossary(const std::string& key);
// The memory region an address belongs to (ROM, VRAM, WRAM, OAM, IO, ...).
Explanation regionExplanation(std::uint16_t address);
// A specific hardware register ($FF00-$FFFF), when the glossary has one.
Explanation ioRegisterExplanation(std::uint16_t address);
// Every key in the glossary (for tests).
std::size_t glossarySize();
bool glossaryHas(const std::string& key);
} // namespace observatory
