; Original Console Observatory boot program (MIT). NOT Nintendo's boot ROM:
; it shows no logo, plays no sound, and checks nothing in the cartridge header.
; It leaves the state a monochrome Game Boy (DMG) is documented to have when
; its boot ROM hands over (Pan Docs "Power Up Sequence"), because cartridges
; rely on it: the LCD is on, so games that wait for VBlank do not hang, and
; A = $01 tells software it is running on an original Game Boy.
ORG $0000
    ld sp, $FFFE
    xor a
    ldh [$FF40], a          ; LCD off while VRAM is cleared
    ld hl, $9FFF
clear_vram:
    ld [hl-], a             ; $9FFF down to $8000
    bit 7, h
    jr nz, clear_vram
    ld a, $80
    ldh [$FF26], a          ; NR52: sound circuits on
    ldh [$FF11], a          ; NR11: channel 1 duty
    ld a, $F3
    ldh [$FF12], a          ; NR12: channel 1 envelope
    ldh [$FF25], a          ; NR51: channel panning
    ld a, $77
    ldh [$FF24], a          ; NR50: master volume
    ld a, $FC
    ldh [$FF47], a          ; BGP
    ld a, $91
    ldh [$FF40], a          ; LCDC: LCD and background on
    ld bc, $01B0            ; F = $B0: Z, H, and C set...
    ld a, [$014D]
    or a
    jr nz, flags_ready
    ld c, $80               ; ...but H and C clear when the header checksum is $00
flags_ready:
    push bc
    pop af
    ld bc, $0013
    ld de, $00D8
    ld hl, $014D
    jp handoff
ORG $00FC
handoff:
    ld a, $01               ; A = $01: original Game Boy (DMG)
    ldh [$FF50], a          ; unmap this program; the next fetch is cartridge $0100
