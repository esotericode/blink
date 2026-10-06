; Console Observatory's original bank-switching demo (MIT).
; Cartridge: MBC1 + RAM + battery; four 16 KiB ROM banks; 8 KiB cartridge RAM.
;
; The CPU sees only 32 KiB of cartridge ROM: bank 0 is fixed at $0000-$3FFF,
; and the MBC1 chip decides which other bank appears at $4000-$7FFF.
; Press A: the program writes the next bank number (1 -> 2 -> 3 -> 1) to
; $2000, an MBC1 register, then calls $4000. Each bank stores its own routine
; there, which copies that bank's 8x8 pattern into tile 1. The background map
; is all tile 1, so the whole screen shows which bank was last selected.
; Every switch also increments a counter in battery-backed cartridge RAM,
; guarded by a signature byte as real games guard their saves.
DEF current_bank EQU $C000
DEF buttons EQU $C001
DEF previous EQU $C002
DEF switches EQU $C003
DEF saved_count EQU $A000
DEF save_signature EQU $A001
DEF pattern_tile EQU $8010
ORG $0100
    nop
    jp start
ORG $0150
start:
    di
    ld sp, $DFFF
wait_for_lcd_off:
    ldh a, [$FF44]          ; turn the LCD off only during VBlank
    cp 144
    jr c, wait_for_lcd_off
    xor a
    ldh [$FF40], a
    ld a, $0A
    ld [$0000], a           ; enable cartridge RAM to check the save
    ld a, [save_signature]
    cp $42                  ; cartridge RAM powers up with unknown contents, so
    jr z, save_ok           ; trust the counter only if our signature is there
    xor a
    ld [saved_count], a
    ld a, $42
    ld [save_signature], a
save_ok:
    xor a
    ld [$0000], a           ; disable cartridge RAM
    ld hl, $C000
    ld b, 16
clear_variables:
    ld [hl+], a
    dec b
    jr nz, clear_variables
    ld hl, $9800
    ld bc, 1024
fill_map:
    ld a, 1                 ; every map entry shows tile 1
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, fill_map
    ld a, 1
    ld [current_bank], a
    ld [$2000], a           ; MBC1: map ROM bank 1 at $4000-$7FFF
    call $4000              ; bank 1's routine draws its pattern
    ld a, $E4
    ldh [$FF47], a
    ld a, $91
    ldh [$FF40], a
main_loop:
wait_visible:
    ldh a, [$FF44]
    cp 144
    jr nc, wait_visible
wait_vblank:
    ldh a, [$FF44]
    cp 144
    jr c, wait_vblank
    ld a, $10               ; select the action buttons (A, B, Select, Start)
    ldh [$FF00], a
    ldh a, [$FF00]
    ldh a, [$FF00]
    cpl
    and $0F
    ld b, a
    ld a, [buttons]
    ld [previous], a
    ld a, b
    ld [buttons], a
    bit 0, a                ; A held now...
    jr z, main_loop
    ld a, [previous]
    bit 0, a                ; ...but not last frame: a new press
    jr nz, main_loop
    ld a, [current_bank]
    cp 3
    jr c, next_bank
    xor a
next_bank:
    inc a
    ld [current_bank], a
write_rom_bank:
    ld [$2000], a           ; MBC1 register: which bank appears at $4000-$7FFF
call_bank:
    call $4000              ; same address, different code: whatever bank is mapped
    ld a, [switches]
    inc a
    ld [switches], a
    ld a, $0A
write_ram_enable:
    ld [$0000], a           ; MBC1 register: $0A enables cartridge RAM
    ld a, [saved_count]
    inc a
write_saved_count:
    ld [saved_count], a     ; battery-backed: kept in a .sav file between sessions
    xor a
write_ram_disable:
    ld [$0000], a           ; disable cartridge RAM again, as games do to protect saves
    jr main_loop

BANK 1
ORG $4000
bank1_routine:
    ld de, bank1_pattern
    ld hl, pattern_tile
    ld b, 16
bank1_copy:
    ld a, [de]
    inc de
    ld [hl+], a
    dec b
    jr nz, bank1_copy
    ret
bank1_pattern:
    ; Horizontal stripes in colour 1.
    DB $FF,$00,$00,$00,$FF,$00,$00,$00,$FF,$00,$00,$00,$FF,$00,$00,$00

BANK 2
ORG $4000
bank2_routine:
    ld de, bank2_pattern
    ld hl, pattern_tile
    ld b, 16
bank2_copy:
    ld a, [de]
    inc de
    ld [hl+], a
    dec b
    jr nz, bank2_copy
    ret
bank2_pattern:
    ; Checkerboard in colours 2 and 0.
    DB $00,$F0,$00,$F0,$00,$F0,$00,$F0,$00,$0F,$00,$0F,$00,$0F,$00,$0F

BANK 3
ORG $4000
bank3_routine:
    ld de, bank3_pattern
    ld hl, pattern_tile
    ld b, 16
bank3_copy:
    ld a, [de]
    inc de
    ld [hl+], a
    dec b
    jr nz, bank3_copy
    ret
bank3_pattern:
    ; Diagonal line in colour 3.
    DB $80,$80,$40,$40,$20,$20,$10,$10,$08,$08,$04,$04,$02,$02,$01,$01
