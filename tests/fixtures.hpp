#pragma once
#include "teaching_rom.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

// Original hand-assembled fixture: the "shadow OAM" pattern real games use.
// Fill $C100-$C19F with 0..159, copy a DMA routine into HRAM, enable sprites,
// then once per frame bump shadow sprite 0's X ($C101) and run the routine.
// The routine writes $C1 to $FF46 at $FF82 and busy-waits in HRAM while the
// hardware copies (on DMG the CPU cannot fetch from ROM during the copy).
// With `restart`, the routine writes $FF46 twice in a row.
inline std::array<std::uint8_t, 32768> dmaFixture(bool restart = false) {
    auto rom = observatory::demo::rom;
    const std::uint8_t code[] = {
        0xF3,             // 0150 di
        0x31, 0xFE, 0xFF, // 0151 ld sp,$FFFE
        0x21, 0x00, 0xC1, // 0154 ld hl,$C100
        0x06, 0xA0,       // 0157 ld b,160
        0xAF,             // 0159 xor a
        0x22,             // 015A fill: ld [hl+],a
        0x3C,             // 015B inc a
        0x05,             // 015C dec b
        0x20, 0xFB,       // 015D jr nz,fill
        0x21, 0x80, 0xFF, // 015F ld hl,$FF80
        0x11, 0xA0, 0x01, // 0162 ld de,routine ($01A0)
        0x06, 0x0C,       // 0165 ld b,12
        0x1A,             // 0167 copy: ld a,[de]
        0x13,             // 0168 inc de
        0x22,             // 0169 ld [hl+],a
        0x05,             // 016A dec b
        0x20, 0xFA,       // 016B jr nz,copy
        0x3E, 0x93,       // 016D ld a,$93 (LCD, background, sprites on)
        0xE0, 0x40,       // 016F ldh [$40],a
        0xCD, 0x80, 0xFF, // 0171 call $FF80
        0xF0, 0x44,       // 0174 loop: ldh a,[$44]
        0xFE, 0x90,       // 0176 cp 144
        0x20, 0xFA,       // 0178 jr nz,loop
        0x21, 0x01, 0xC1, // 017A ld hl,$C101
        0x34,             // 017D inc [hl]
        0xCD, 0x80, 0xFF, // 017E call $FF80
        0xF0, 0x44,       // 0181 wait: ldh a,[$44]
        0xFE, 0x90,       // 0183 cp 144
        0x28, 0xFA,       // 0185 jr z,wait
        0x18, 0xEB,       // 0187 jr loop
    };
    std::copy(std::begin(code), std::end(code), rom.begin() + 0x150);
    // HRAM routine at $FF80: ld a,$C1; ldh [$46],a; [ldh [$46],a;] ld a,40; dec a; jr nz; ret
    const std::uint8_t once[] = {0x3E, 0xC1, 0xE0, 0x46, 0x3E, 0x28, 0x3D, 0x20, 0xFD, 0xC9, 0x00, 0x00};
    const std::uint8_t twice[] = {0x3E, 0xC1, 0xE0, 0x46, 0xE0, 0x46, 0x3E, 0x28, 0x3D, 0x20, 0xFD, 0xC9};
    std::copy(std::begin(restart ? twice : once), std::end(restart ? twice : once), rom.begin() + 0x1A0);
    return rom;
}
