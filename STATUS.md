# Status — 2026-10-06

Native desktop teaching lab for Linux x86-64 and Windows x64. C++20 / Qt 6
Widgets / CMake / unchanged SameBoy 1.0.3 DMG-B, commit
`208ba4afabffab9edde416f2dbb8ae459e34adb8`. Version 0.2.0.

## Works

- Offline launch with the embedded original teaching game visible. Arrow input
  changes named WRAM position variables, OAM X/Y, and the star's real output.
- **Guided lesson** "Follow one press of Right": hold Right → stop after the
  `player_x` store → stop after the OAM X store (outline ahead of the
  unchanged picture) → next frame with changed pixels marked → the tile's bit
  planes. Every stop is a real emulator event; text uses the real evidence.
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

Linux (Ubuntu 24.04, Qt 6.4.2): four CTest suites pass, including nine engine
groups (trace parity with VRAM/OAM/previous output, stepping, purity, IRQ/HALT,
bounded capture, tile decoding against source bytes and rendered colours,
run-until-write, its parity with untraced stepping, activity map) and the
widget suite, which drives the whole lesson through its buttons. Widgets also
pass under X11 at 1600×1000 and on a 640×480 screen (window clamps to its
980×680 minimum). Rendered layouts inspected at default and minimum sizes and
at every lesson stage. Install tree and `.deb` generated.

Windows: cross-compiled locally with Ubuntu MinGW-w64 GCC 13 against Qt 6.4.2
qtbase built from source; engine and widget suites pass under Wine 9.0 with
Qt's native `windows` platform plugin, and the flat packaged layout launches
from a clean folder. GitHub Actions (`windows-2025`, official Qt 6.8.3 MinGW
+ MinGW 13.1) builds, passes all four suites and the native-platform widget
run, packages a 12 MB portable ZIP with windeployqt, and launches the unzipped
app with only System32 on `PATH`.

## Limits

32 KiB ROM-only DMG; teaching boot rather than hardware startup; raw IO storage
rather than bus readback; operand-storage observations; CPU write attempts
and opcode starts only, without read, DMA, or PPU access hooks; completed-frame
display rather than pixel provenance; no replay/rewind, audio playback, gate
model, or automatic full causality. Not yet tested: a physical Windows desktop
or real high-DPI monitors, Windows installer/signing, macOS, external
emulator test-ROM suites, clean-machine `.deb` installation.

## Next concrete milestone

An interrupt and timer lesson: show VBlank/STAT/timer requests and service on a
timeline built from real IF/IE storage and execution events (label what is not
observed), and a background-map view linking each map entry to its tile. Add
verified DMA tracing before lessons for DMA-based games. Validate the Windows
ZIP on a physical Windows 10/11 machine and record the result.

Read `PROJECT_GUIDE.md`, `DECISIONS.md`, `AGENTS.md`, and
`docs/TRACE_CONTRACT.md` before changing observation behavior.
