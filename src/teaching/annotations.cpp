#include "teaching/annotations.hpp"
#include "teaching_rom.hpp"
#include "emulator/state.hpp"
namespace observatory {
Program programOf(const Snapshot& s) {
    return s.teaching ? Program::Teaching : s.bankDemo ? Program::BankDemo : Program::Other;
}
std::string addressName(std::uint16_t address, Program program) {
    auto a = canonicalAddress(address);
    if (program == Program::Teaching) {
        if (a == demo::player_x) return "player_x";
        if (a == demo::player_y) return "player_y";
        if (a == demo::buttons) return "buttons";
        if (a == demo::frame_counter) return "frame_counter";
    }
    if (program == Program::BankDemo) {
        if (a == bankdemo::current_bank) return "current_bank";
        if (a == bankdemo::buttons) return "buttons";
        if (a == bankdemo::previous) return "previous buttons";
        if (a == bankdemo::switches) return "switches";
        if (a == bankdemo::saved_count) return "saved_count (battery RAM)";
        if (a == bankdemo::save_signature) return "save_signature (battery RAM)";
        if (a >= bankdemo::pattern_tile && a < bankdemo::pattern_tile + 16) return "tile 1: the bank's pattern";
    }
    switch (a) {
    case 0xFE00: return "Sprite 0: Y + 16";
    case 0xFE01: return "Sprite 0: X + 8";
    case 0xFE02: return "Sprite 0: tile";
    case 0xFE03: return "Sprite 0: flags";
    case 0xFF00: return "JOYP: button register";
    case 0xFF40: return "LCDC: display control";
    case 0xFF44: return "LY: scanline";
    case 0xFF50: return "Boot program unmap";
    default: return {};
    }
}
std::string instructionNote(std::uint16_t pc, std::uint16_t bank, Program program) {
    if (program == Program::Teaching) {
        if (pc == demo::write_player_x_right) return "Teaching source: Right increments player_x by one, within the screen bounds.";
        if (pc == demo::write_player_x_left) return "Teaching source: Left decrements player_x by one.";
        if (pc == demo::write_oam_x) return "Teaching source: copy player_x + 8 into sprite 0's OAM X byte.";
        if (pc == demo::write_oam_y) return "Teaching source: copy player_y + 16 into sprite 0's OAM Y byte.";
    }
    if (program == Program::BankDemo) {
        if (pc == bankdemo::write_rom_bank) return "Bank demo source: write the next bank number to the MBC1 bank register ($2000-$3FFF).";
        if (pc == bankdemo::call_bank) return "Bank demo source: CALL $4000 runs whichever bank is mapped there now.";
        if (pc == bankdemo::write_ram_enable) return "Bank demo source: $0A written to $0000-$1FFF enables cartridge RAM (MBC1).";
        if (pc == bankdemo::write_saved_count) return "Bank demo source: store the switch count in battery-backed cartridge RAM.";
        if (pc == bankdemo::write_ram_disable) return "Bank demo source: disable cartridge RAM again to protect the save.";
        if (pc >= 0x4000 && pc < bankdemo::bank1_pattern && bank >= 1 && bank <= 3) {
            return "Bank demo source: bank " + std::to_string(bank) + "'s routine at $4000 copies bank " + std::to_string(bank) + "'s pattern into tile 1.";
        }
    }
    return {};
}
std::string regionName(std::uint16_t a) {
    if (a < 0x4000) return "Cartridge ROM, fixed window";
    if (a < 0x8000) return "Cartridge ROM, switchable window";
    if (a < 0xA000) return "VRAM: tile data and maps";
    if (a < 0xC000) return "Cartridge RAM window";
    if (a < 0xE000) return "WRAM: work RAM";
    if (a < 0xFE00) return "Echo of WRAM $C000-$DDFF";
    if (a < 0xFEA0) return "OAM: sprite records";
    if (a < 0xFF00) return "Unusable";
    if (a < 0xFF80) return "IO registers";
    if (a < 0xFFFF) return "HRAM: high RAM";
    return "IE: interrupt enable";
}
}
