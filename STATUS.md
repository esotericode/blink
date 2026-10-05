# Status — 2026-10-05

First working native desktop slice implemented and locally verified on Ubuntu
24.04 x86-64. C++20 / Qt 6.4.2 Widgets / CMake 3.28.3 / unchanged SameBoy 1.0.3
DMG-B, commit `208ba4afabffab9edde416f2dbb8ae459e34adb8`.

## Works

- Offline launch with the embedded original teaching game visible. Arrow input
  changes named WRAM position variables, OAM X/Y, and the star's real frame output.
- Native run/pause, one-opcode step, next-output frame step; resizable game,
  register, memory, and write inspectors; recoverable layout and focus handling.
- Shared instruction-boundary CPU/raw-storage snapshots, storage disassembly,
  change highlighting, source-gated semantic names, bounded last-writer evidence,
  and selection linking between memory and captured writes.
- Separate frame timestamps, labeled live interval summaries, write-attempt
  labeling, unknown IRQ attribution, and visible history evictions.
- Reproducible assembly/symbols/checksums/embedding without RGBDS; reference
  dependencies, license notices, Linux install/desktop launcher/`.deb`, and CI.

## Verified

Four CTest suites pass (about 1.2 seconds here): full serialized-state trace
parity over 90 controlled frames; real one-pixel Right movement through WRAM
and OAM; all-address-space inspection purity; pause/step synchronization;
300-frame bounded capture; IRQ/HALT semantics; ownership; native Qt controls,
focus, writer selection and responsive timers; ROM reproducibility; 51 unchanged
upstream file hashes. Qt tests ran offscreen and under virtual X11.

The installed production executable launched/rendered its embedded game from
an unrelated directory. Default/minimum-size screenshots were inspected.
Install output has no build RUNPATH; `.deb` control/data were inspected.
Exact evidence and commands: `docs/VERIFICATION.md`.

Independent GitHub Actions run 2 passed the Linux build, all four suites,
ordinary `xvfb-run` native interactions, and install/package generation for
implementation commit `a05d42798cc4c4f6cc00ffdfe314d66b85c24ea3`.

## Limits

32 KiB ROM-only DMG; teaching boot rather than hardware startup; raw IO storage
rather than bus readback; operand-storage observations; CPU write attempts only,
without DMA/PPU access hooks; completed-frame display rather than pixel provenance;
no replay/rewind, audio playback, gate model, or automatic full causality. No
external emulator test-ROM suite, physical compositor, clean-machine package
installation, other Qt version, macOS, or Windows validation has been run.

## Next concrete milestone

Add a bounded pause-on-write movement lesson: stop at the `player_x` store,
follow the OAM X store, advance to its visible frame, and provide a real-storage
sprite/tile bitplane inspector. Keep stages tagged by actual cursors/output
boundaries. Add verified DMA tracing before lessons for DMA-based games.

Read `PROJECT_GUIDE.md`, `DECISIONS.md`, `AGENTS.md`, and
`docs/TRACE_CONTRACT.md` before changing observation behavior. The first-slice
plan is complete; future features should preserve its checks.
