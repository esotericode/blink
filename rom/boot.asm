; Original minimal teaching boot. NOT a hardware-accurate power-on sequence.
; No header/logo validation. The teaching game initializes its required hardware.
ORG $0000
    jp handoff
ORG $00FC
handoff:
    ld a, 1
    ldh [$FF50], a
; FF50 disables boot mapping; the next fetch is from cartridge $0100.
