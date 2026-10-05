#include "teaching/annotations.hpp"
#include "teaching_rom.hpp"
#include "emulator/state.hpp"
namespace observatory {
std::string addressName(std::uint16_t address, bool teaching) {
    auto a = canonicalAddress(address);
    if (teaching) {
        if (a == demo::player_x) return "player_x";
        if (a == demo::player_y) return "player_y";
        if (a == demo::buttons) return "buttons";
        if (a == demo::frame_counter) return "frame_counter";
    }
    switch (a) {
    case 0xFE00: return "Sprite 0: Y + 16";
    case 0xFE01: return "Sprite 0: X + 8";
    case 0xFE02: return "Sprite 0: tile";
    case 0xFE03: return "Sprite 0: flags";
    case 0xFF00: return "JOYP: button register";
    case 0xFF40: return "LCDC: display control";
    case 0xFF44: return "LY: scanline";
    default: return {};
    }
}
std::string instructionNote(std::uint16_t pc, bool teaching) {
    if (!teaching) return {};
    if (pc == demo::write_player_x_right) return "Teaching source: Right increments player_x by one, within the screen bounds.";
    if (pc == demo::write_player_x_left) return "Teaching source: Left decrements player_x by one.";
    if (pc == demo::write_oam_x) return "Teaching source: copy player_x + 8 into sprite 0's OAM X byte.";
    if (pc == demo::write_oam_y) return "Teaching source: copy player_y + 16 into sprite 0's OAM Y byte.";
    return {};
}
}
