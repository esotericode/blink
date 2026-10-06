#include "emulator/graphics.hpp"
#include "gb.h"

namespace observatory {
bool Sprite::onScreen(int height) const {
    return screenX() + 8 > 0 && screenX() < 160 && screenY() + height > 0 && screenY() < 144;
}
Sprite sprite(const Oam& oam, int index) {
    const auto base = std::size_t(index) * 4;
    return {index, oam[base], oam[base + 1], oam[base + 2], oam[base + 3]};
}
std::uint16_t tileAddress(int tileNumber) { return std::uint16_t(0x8000 + tileNumber * 16); }
int backgroundTile(std::uint8_t mapEntry, bool unsignedAddressing) {
    return unsignedAddressing || mapEntry >= 128 ? mapEntry : 256 + mapEntry;
}
std::uint8_t colorIndex(std::uint8_t low, std::uint8_t high, int column) {
    const int bit = 7 - column;
    return std::uint8_t(((high >> bit) & 1) << 1 | ((low >> bit) & 1));
}
std::array<std::uint8_t, 64> decodeTile(const Vram& vram, int tileNumber) {
    std::array<std::uint8_t, 64> out{};
    const auto base = std::size_t(tileNumber) * 16;
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            out[row * 8 + column] = colorIndex(vram[base + row * 2], vram[base + row * 2 + 1], column);
        }
    }
    return out;
}
std::array<std::uint32_t, 4> shadeColors() {
    // GB_update_dmg_palette maps shade 0 to colors[3] ... shade 3 to colors[0],
    // and the engine's encode callback packs them as opaque 0xFFRRGGBB.
    std::array<std::uint32_t, 4> out{};
    for (int s = 0; s < 4; ++s) {
        const auto& c = GB_PALETTE_DMG.colors[3 - s];
        out[s] = 0xFF000000u | std::uint32_t(c.r) << 16 | std::uint32_t(c.g) << 8 | c.b;
    }
    return out;
}
} // namespace observatory
