#include "emulator/cartridge.hpp"
#include <algorithm>
#include <cstring>

namespace observatory {
namespace {
struct Type { std::uint8_t code; Mbc mbc; const char* name; bool ram, battery, timer, rumble, supported; };
// Pan Docs cartridge types; `supported` mirrors SameBoy 1.0.3's GB_cart_defs.
constexpr Type types[] = {
    {0x00, Mbc::None, "ROM ONLY", false, false, false, false, true},
    {0x01, Mbc::Mbc1, "MBC1", false, false, false, false, true},
    {0x02, Mbc::Mbc1, "MBC1+RAM", true, false, false, false, true},
    {0x03, Mbc::Mbc1, "MBC1+RAM+BATTERY", true, true, false, false, true},
    {0x05, Mbc::Mbc2, "MBC2", true, false, false, false, true},
    {0x06, Mbc::Mbc2, "MBC2+BATTERY", true, true, false, false, true},
    {0x08, Mbc::None, "ROM+RAM", true, false, false, false, true},
    {0x09, Mbc::None, "ROM+RAM+BATTERY", true, true, false, false, true},
    {0x0B, Mbc::Mmm01, "MMM01", false, false, false, false, true},
    {0x0C, Mbc::Mmm01, "MMM01+RAM", true, false, false, false, true},
    {0x0D, Mbc::Mmm01, "MMM01+RAM+BATTERY", true, true, false, false, true},
    {0x0F, Mbc::Mbc3, "MBC3+TIMER+BATTERY", false, true, true, false, true},
    {0x10, Mbc::Mbc3, "MBC3+TIMER+RAM+BATTERY", true, true, true, false, true},
    {0x11, Mbc::Mbc3, "MBC3", false, false, false, false, true},
    {0x12, Mbc::Mbc3, "MBC3+RAM", true, false, false, false, true},
    {0x13, Mbc::Mbc3, "MBC3+RAM+BATTERY", true, true, false, false, true},
    {0x19, Mbc::Mbc5, "MBC5", false, false, false, false, true},
    {0x1A, Mbc::Mbc5, "MBC5+RAM", true, false, false, false, true},
    {0x1B, Mbc::Mbc5, "MBC5+RAM+BATTERY", true, true, false, false, true},
    {0x1C, Mbc::Mbc5, "MBC5+RUMBLE", false, false, false, true, true},
    {0x1D, Mbc::Mbc5, "MBC5+RUMBLE+RAM", true, false, false, true, true},
    {0x1E, Mbc::Mbc5, "MBC5+RUMBLE+RAM+BATTERY", true, true, false, true, true},
    {0x20, Mbc::Mbc6, "MBC6", true, true, false, false, false},
    {0x22, Mbc::Mbc7, "MBC7+SENSOR+RUMBLE+RAM+BATTERY", true, true, false, true, true},
    {0xFC, Mbc::Camera, "POCKET CAMERA", true, true, false, false, true},
    {0xFD, Mbc::Tama5, "BANDAI TAMA5", true, true, true, false, false},
    {0xFE, Mbc::Huc3, "HuC3", true, true, true, false, true},
    {0xFF, Mbc::Huc1, "HuC1+RAM+BATTERY", true, true, false, false, true},
};
std::size_t ramBytes(std::uint8_t code) {
    switch (code) {
    case 0x01: return 2 * 1024; // unofficial, seen in some homebrew
    case 0x02: return 8 * 1024;
    case 0x03: return 32 * 1024;
    case 0x04: return 128 * 1024;
    case 0x05: return 64 * 1024;
    default: return 0;
    }
}
}

std::string mbcName(Mbc mbc) {
    switch (mbc) {
    case Mbc::None: return "no MBC";
    case Mbc::Mbc1: return "MBC1";
    case Mbc::Mbc2: return "MBC2";
    case Mbc::Mbc3: return "MBC3";
    case Mbc::Mbc5: return "MBC5";
    case Mbc::Mbc6: return "MBC6";
    case Mbc::Mbc7: return "MBC7";
    case Mbc::Mmm01: return "MMM01";
    case Mbc::Huc1: return "HuC1";
    case Mbc::Huc3: return "HuC3";
    case Mbc::Camera: return "Pocket Camera";
    case Mbc::Tama5: return "TAMA5";
    case Mbc::Tpp1: return "TPP1";
    default: return "unknown controller";
    }
}

CartridgeInfo describeCartridge(std::span<const std::uint8_t> rom) {
    CartridgeInfo info;
    info.fileBytes = rom.size();
    if (rom.size() < 0x150) {
        info.supported = false;
        info.note = "Too small to contain a cartridge header.";
        return info;
    }
    for (std::size_t i = 0x134; i < 0x144; ++i) {
        const auto c = rom[i];
        if (c == 0) break;
        if (i >= 0x13F && (rom[0x143] & 0x80)) break; // newer headers reuse the end of the title
        info.title += (c >= 0x20 && c < 0x7F) ? char(c) : '?';
    }
    info.cgbFlag = rom[0x143];
    info.sgbFlag = rom[0x146];
    info.type = rom[0x147];
    info.romSizeCode = rom[0x148];
    info.ramSizeCode = rom[0x149];
    info.headerChecksum = rom[0x14D];
    std::uint8_t checksum = 0;
    for (std::size_t i = 0x134; i < 0x14D; ++i) checksum = std::uint8_t(checksum - rom[i] - 1);
    info.headerChecksumValid = checksum == info.headerChecksum;
    info.headerRomBytes = info.romSizeCode <= 0x08 ? std::size_t(32 * 1024) << info.romSizeCode : 0;
    info.headerRamBytes = ramBytes(info.ramSizeCode);
    const auto found = std::find_if(std::begin(types), std::end(types), [&](const Type& t) { return t.code == info.type; });
    if (found != std::end(types)) {
        info.mbc = found->mbc; info.typeName = found->name; info.ram = found->ram; info.battery = found->battery;
        info.timer = found->timer; info.rumble = found->rumble; info.supported = found->supported;
    } else if (info.type == 0xBC && rom[0x149] == 0xC1 && rom[0x14A] == 0x65) {
        info.mbc = Mbc::Tpp1; info.typeName = "TPP1"; info.ram = info.battery = info.timer = info.rumble = true;
    } else {
        info.mbc = Mbc::Unknown; info.typeName = "unknown type"; info.supported = false;
    }
    if (info.mbc == Mbc::Mbc2) info.headerRamBytes = 512; // built-in 512 x 4-bit RAM
    // The same heuristics SameBoy applies when the header and file disagree.
    if (info.mbc == Mbc::None && rom.size() > 0x8000) {
        info.mbc = Mbc::Mbc3;
        info.note = "The header says there is no MBC, but the file is larger than 32 KiB; SameBoy treats it as MBC3.";
    } else if (rom.size() >= 0x8000 && info.mbc != Mbc::Mmm01 &&
               std::memcmp(rom.data() + 0x104, rom.data() + rom.size() - 0x8000 + 0x104, 0x30) == 0 &&
               (rom[rom.size() - 0x8000 + 0x147] == 0x0B || rom[rom.size() - 0x8000 + 0x147] == 0x0C ||
                rom[rom.size() - 0x8000 + 0x147] == 0x0D) && rom.size() > 0x8000) {
        info.mbc = Mbc::Mmm01; info.typeName = "MMM01 (multicart)";
        info.note = "Multicart detected from the header at the end of the file; SameBoy uses MMM01.";
    }
    if (info.mbc == Mbc::Mmm01 && info.note.empty()) {
        info.note = "MMM01 multicarts are rearranged in emulator memory; bank file offsets shown may not match the file.";
    }
    if (!info.supported && info.note.empty()) {
        info.note = "SameBoy does not emulate this cartridge hardware; the game will likely not work.";
    }
    return info;
}

std::string mbcRegisterName(Mbc mbc, std::uint16_t a) {
    if (a >= 0x8000) return {};
    switch (mbc) {
    case Mbc::Mbc1:
        if (a < 0x2000) return "MBC1 RAM enable ($0A enables)";
        if (a < 0x4000) return "MBC1 ROM bank select (low 5 bits)";
        if (a < 0x6000) return "MBC1 RAM bank / upper ROM bank bits";
        return "MBC1 banking mode";
    case Mbc::Mbc2:
        return a < 0x4000 ? ((a & 0x100) ? "MBC2 ROM bank select (address bit 8 = 1)" : "MBC2 RAM enable (address bit 8 = 0)")
                          : "MBC2 (no register here)";
    case Mbc::Mbc3:
        if (a < 0x2000) return "MBC3 RAM and clock enable ($0A enables)";
        if (a < 0x4000) return "MBC3 ROM bank select";
        if (a < 0x6000) return "MBC3 RAM bank or clock register select";
        return "MBC3 clock latch";
    case Mbc::Mbc5:
        if (a < 0x2000) return "MBC5 RAM enable ($0A enables)";
        if (a < 0x3000) return "MBC5 ROM bank select (low 8 bits)";
        if (a < 0x4000) return "MBC5 ROM bank select (bit 8)";
        if (a < 0x6000) return "MBC5 RAM bank select";
        return "MBC5 (no register here)";
    case Mbc::None:
        return {};
    default:
        return mbcName(mbc) + " control register";
    }
}
} // namespace observatory
