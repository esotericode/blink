; Console Observatory's original DMG teaching game. MIT license.
; Built by tools/assemble_rom.py; strict RGBDS-style subset, no external assembler.
; Screen x = player_x, OAM x = player_x + 8; updated once per VBlank.
DEF player_x EQU $C000
DEF player_y EQU $C001
DEF buttons EQU $C002
DEF frame_counter EQU $C003
ORG $0100
    nop
    jp start
ORG $0150
start:
    di
    ld sp, $DFFF
    xor a
    ldh [$FF40], a
    ldh [$FFFF], a
    ldh [$FF0F], a
    ldh [$FF26], a
    ld hl, $C000
    ld b, 32
clear_work:
    ld [hl+], a
    dec b
    jr nz, clear_work
    ld hl, $FE00
    ld b, 160
clear_oam:
    ld [hl+], a
    dec b
    jr nz, clear_oam
    ld hl, $8000
    ld bc, $2000
clear_vram:
    xor a
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, clear_vram
    ld de, tiles
    ld hl, $8000
    ld b, 48
copy_tiles:
    ld a, [de]
    inc de
    ld [hl+], a
    dec b
    jr nz, copy_tiles
    ld hl, $9800
    ld bc, 1024
    ld d, 0
fill_map:
    ld a, d
    and 1
    ld [hl+], a
    inc d
    dec bc
    ld a, b
    or c
    jr nz, fill_map
    ld a, 72
    ld [player_x], a
    ld a, 64
    ld [player_y], a
    ld a, 80
    ld [$FE00], a
    ld a, 80
    ld [$FE01], a
    ld a, 2
    ld [$FE02], a
    ld a, $E4
    ldh [$FF47], a
    ldh [$FF48], a
    ld a, $93
    ldh [$FF40], a
wait_visible:
    ldh a, [$FF44]
    cp 144
    jr nc, wait_visible
wait_vblank:
    ldh a, [$FF44]
    cp 144
    jr c, wait_vblank
poll_input:
    ld a, $20
    ldh [$FF00], a
    ldh a, [$FF00]
    ldh a, [$FF00]
    cpl
    and $0F
    ld [buttons], a
    bit 0, a
    jr z, try_left
    ld a, [player_x]
    cp 152
    jr nc, try_left
    inc a
write_player_x_right:
    ld [player_x], a
try_left:
    ld a, [buttons]
    bit 1, a
    jr z, try_up
    ld a, [player_x]
    or a
    jr z, try_up
    dec a
write_player_x_left:
    ld [player_x], a
try_up:
    ld a, [buttons]
    bit 2, a
    jr z, try_down
    ld a, [player_y]
    or a
    jr z, try_down
    dec a
    ld [player_y], a
try_down:
    ld a, [buttons]
    bit 3, a
    jr z, update_sprite
    ld a, [player_y]
    cp 136
    jr nc, update_sprite
    inc a
    ld [player_y], a
update_sprite:
    ld a, [player_y]
    add a, 16
write_oam_y:
    ld [$FE00], a
    ld a, [player_x]
    add a, 8
write_oam_x:
    ld [$FE01], a
    ld a, [frame_counter]
    inc a
    ld [frame_counter], a
    jr wait_visible
tiles:
    ; Two gentle background tiles, then an original 8x8 star sprite.
    DB $00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$01,$00
    DB $00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00
    DB $18,$18,$18,$18,$7E,$7E,$3C,$3C,$3C,$3C,$7E,$7E,$18,$18,$18,$18
