# Status — 2026-10-06

Native desktop teaching lab for Linux x86-64 and Windows x64. C++20 / Qt 6
Widgets / CMake / unchanged SameBoy 1.0.3 DMG-B, commit
`208ba4afabffab9edde416f2dbb8ae459e34adb8`. Version 0.3.0.

## Works

- Offline launch with the embedded original teaching game visible. Arrow input
  changes named WRAM position variables, OAM X/Y, and the star's real output.
- **Guided lesson** "Follow one press of Right": hold Right → stop after the
  `player_x` store → stop after the OAM X store (outline ahead of the
  unchanged picture) → next frame with changed pixels marked → the tile's bit
  planes. Every stop is a real emulator event; text uses the real evidence.
- **Any Game Boy ROM** (File › Open, drag and drop), up to 8 MiB, with every
  controller SameBoy emulates. The original boot leaves the documented DMG
  post-boot state, which real games rely on. CGB-only, SGB, clock, MBC2, and
  header/file mismatches are explained in the Cartridge panel; MBC6, TAMA5,
  and unknown types are refused before the current game is touched.
- **Cartridge · banks** panel: CPU windows linked to the mapped ROM/RAM banks,
  per-bank opcode shading, an explanation adapted to the controller, MBC
  register writes with their effect, and **Run until the bank changes**.
  Memory and CPU panels name the mapped bank and file offset; the captured
  write list and writer text describe MBC commands instead of ROM "values".
- **Bank-switching demo**: an original MBC1 cartridge (same address, different
  bank code) with a two-page lesson that stops at the real switch.
- **Battery saves**: `.sav` next to the ROM in SameBoy's format, loaded on
  open, written atomically every ~3 s while dirty, on game change, and on exit.
- **Run until written** (F9) for any byte, stopping at the writing step's end.
- **Sprites and tiles**: OAM records, 128-tile VRAM blocks, and bit-plane rows
  that combine into colour numbers and palette shades, from copied storage.
- **Memory map**: per-address CPU write attempts and opcode starts over a
  labelled interval for all 64 KiB, with a page magnifier and click-through.
- **System overview**: functional schematic with live values, write counts,
  lesson path highlighting, and click-through to storage.
- Run/pause, one-opcode step, next-output frame step; dockable panels with a
  recoverable default layout; menu bar, About, licenses, About Qt.
- Shared instruction-boundary snapshots (CPU, memory, VRAM, OAM, video
  registers), storage disassembly, change highlighting, source-gated names,
  bounded last-writer evidence, linked selection across panels.
- Reproducible ROM/symbols/embedding without RGBDS; Linux `.deb` and Windows
  portable ZIP (windeployqt) packaging; Linux and Windows CI.

## Verified

Linux (Ubuntu 24.04, Qt 6.4.2): four CTest suites pass, including eleven
engine groups (post-boot state, trace parity with VRAM/OAM/previous output,
stepping, purity, IRQ/HALT, bounded capture, tile decoding, run-until-write
and its parity with untraced stepping, activity map, an MBC1 cartridge with
bank-change stops, banked disassembly, cartridge RAM and battery restore, and
banked trace parity plus an MBC5 variant and header heuristics) and the widget
suite, which drives both lessons through their buttons, checks the Cartridge
panel, MBC writer text, drag-and-drop loading of an MBC5 ROM with an existing
`.sav`, and battery writes. Widgets also pass under X11. Rendered layouts
inspected at default and minimum sizes, every lesson stage, and every tab.

Windows: cross-compiled locally with Ubuntu MinGW-w64 GCC 13 against Qt 6.4.2
qtbase built from source; the engine suite (all eleven groups) and the widget
suite pass under Wine 9.0 (see `docs/VERIFICATION.md` for this version's
exact Windows results). GitHub Actions (`windows-2025`, official Qt 6.8.3 MinGW
+ MinGW 13.1) builds, passes all four suites and the native-platform widget
run, packages a 12 MB portable ZIP with windeployqt, and launches the unzipped
app with only System32 on `PATH`.

## Limits

DMG model only (no Game Boy Color mode, no SGB); original boot with the
documented post-boot state rather than the hardware boot sequence and timing;
raw IO storage rather than bus readback; operand-storage observations; CPU
write attempts and opcode starts only, without read, DMA, or PPU access hooks
(most commercial games write OAM by DMA, so those sprite writes have no
writer); cartridge RAM shown as storage regardless of the MBC's RAM enable,
and clock registers not shown; MMM01 file offsets approximate; no
per-bank memory map (the `$4000–$7FFF` row combines banks); completed-frame
display rather than pixel provenance; no replay/rewind, save states, audio
playback, gate model, or automatic full causality. Commercial-game
compatibility is SameBoy's and has not been surveyed here. Not yet tested:
a physical Windows desktop or real high-DPI monitors, Windows
installer/signing, macOS, external emulator test-ROM suites, clean-machine
`.deb` installation.

## Next concrete milestone

Now that real games run, DMA visibility matters most: add an observed OAM DMA
event (source page, start/end interval) without changing emulation, so
sprite bytes written by DMA get an honest "copied by DMA from $C1xx" writer.
Then the interrupt and timer lesson (VBlank/STAT/timer requests and service
from real IF/IE storage and execution events) and a background-map view
linking each map entry to its tile. Validate the Windows ZIP on a physical
Windows 10/11 machine and record the result.

Read `PROJECT_GUIDE.md`, `DECISIONS.md`, `AGENTS.md`, and
`docs/TRACE_CONTRACT.md` before changing observation behavior.
