#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Pure decoding of copied DMG video storage (no emulator access). Layouts follow
// Pan Docs "Tile Data", "Object Attribute Memory", and "Palettes".
namespace observatory {
inline constexpr std::size_t vramSize = 0x2000;
inline constexpr int tileCount = 384; // $8000-$97FF, 16 bytes per tile
inline constexpr int spriteCount = 40;
using Vram = std::array<std::uint8_t, vramSize>;
using Oam = std::array<std::uint8_t, 160>;

// One OAM record exactly as stored: Y+16, X+8, tile number, attribute flags.
struct Sprite {
    int index{};
    std::uint8_t y{}, x{}, tile{}, flags{};
    int screenX() const { return int(x) - 8; }
    int screenY() const { return int(y) - 16; }
    bool behindBackground() const { return flags & 0x80; }
    bool flipY() const { return flags & 0x40; }
    bool flipX() const { return flags & 0x20; }
    int palette() const { return (flags >> 4) & 1; } // 0 = OBP0, 1 = OBP1
    // True when any pixel row/column of the object lies inside the 160x144 display.
    bool onScreen(int height) const;
};
Sprite sprite(const Oam& oam, int index);
// Objects always use $8000-based unsigned tile numbers.
std::uint16_t tileAddress(int tileNumber);
// Map entry -> tile number in 0..383, given LCDC bit 4 ($8000 vs $8800 addressing).
int backgroundTile(std::uint8_t mapEntry, bool unsignedAddressing);
// 2bpp row: the low bit plane byte supplies bit 0, the high bit plane byte bit 1;
// bit 7 of each byte is the leftmost pixel.
std::uint8_t colorIndex(std::uint8_t low, std::uint8_t high, int column);
std::array<std::uint8_t, 64> decodeTile(const Vram& vram, int tileNumber);
// A palette register holds a two-bit shade (0 lightest .. 3 darkest) per color index.
inline std::uint8_t shade(std::uint8_t palette, std::uint8_t index) { return (palette >> (index * 2)) & 3; }
// The 0xAARRGGBB colours this application's SameBoy palette produces for shades 0..3.
std::array<std::uint32_t, 4> shadeColors();
} // namespace observatory
