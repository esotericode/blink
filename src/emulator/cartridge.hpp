#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// Cartridge facts decoded from ROM bytes alone (Pan Docs "The Cartridge Header"
// and "MBCs"). No emulator access; the mapped banks come from the engine.
namespace observatory {
enum class Mbc { None, Mbc1, Mbc2, Mbc3, Mbc5, Mbc6, Mbc7, Mmm01, Huc1, Huc3, Camera, Tama5, Tpp1, Unknown };

struct CartridgeInfo {
    std::string title;
    std::uint8_t type{}, romSizeCode{}, ramSizeCode{}, cgbFlag{}, sgbFlag{}, headerChecksum{};
    std::uint8_t effectiveType{};      // controller variant chosen by the pinned core's heuristics
    Mbc mbc = Mbc::None;
    std::string typeName;            // e.g. "MBC1+RAM+BATTERY"
    bool ram{}, battery{}, timer{}, rumble{};
    bool supported = true;           // SameBoy 1.0.3 implements this cartridge hardware
    bool headerChecksumValid{};
    std::size_t fileBytes{};
    std::size_t headerRomBytes{}, headerRamBytes{};
    std::string note;                // Plain-language caveat, empty when none
    int romBanks() const { return int((fileBytes + 0x3FFF) / 0x4000); }
    bool banked() const { return mbc != Mbc::None; }
    bool cgbOnly() const { return cgbFlag == 0xC0; }
    bool cgbEnhanced() const { return cgbFlag == 0x80; }
};

CartridgeInfo describeCartridge(std::span<const std::uint8_t> rom);
std::string mbcName(Mbc mbc);
// What a CPU write to $0000-$7FFF means to this cartridge's controller, e.g.
// "ROM bank select (low 5 bits)". Empty for cartridges without a controller.
std::string mbcRegisterName(Mbc mbc, std::uint16_t address);
} // namespace observatory
