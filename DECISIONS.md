# Decisions and evidence

## 2026-10-05 — First platform and dependencies

- Linux x86-64 first: the execution environment is Ubuntu 24.04 with GCC 13.3.
  Keep platform-independent C++/Qt code; do not promise macOS/Windows packaging.
- Retain C++20 / Qt 6 Widgets / CMake. Use dynamically linked Qt Core, Gui,
  Widgets (and Test only for verification), under LGPLv3. No web modules.
- Vendor only SameBoy's unchanged `Core`, `LICENSE`, and `version.mk`, from
  release 1.0.3, commit `208ba4afabffab9edde416f2dbb8ae459e34adb8`.
  Core is Expat/MIT; the excluded iOS/HexFiend license exceptions do not apply.
  A small CMake target replaces upstream's make/lib/header-cleaning workflow;
  this avoids pulling SDL, cppp, and RGBDS into this slice.
- Use existing execution and write callbacks. The write callback precedes
  acceptance, so a callback alone is evidence of an attempt. Correlate physical
  WRAM/OAM contents before and after the enclosing atomic `GB_run` interval.
- `GB_run` returns 8,388,608 Hz ticks. It usually executes one opcode, but HALT,
  STOP, and interrupt service may advance time without an opcode callback.
  Capture instruction-boundary intervals; make no sub-instruction timing claim.
- Do not use upstream disassembly directly: `GB_cpu_disassemble` uses
  `GB_read_memory`, which is a bus read with side effects. Decode copied
  direct-storage bytes instead, and identify the next view as storage.
- Original teaching ROM and minimal boot program, assembled by a small strict
  Python subset assembler. They initialize the state they require; boot does
  not reproduce the manufacturer's power-on sequence. No Nintendo ROM/logo.

Primary evidence: pinned `Core/gb.c`, `Core/sm83_cpu.c`, `Core/memory.c`,
`Core/sm83_disassembler.c`, upstream `Makefile`, and `LICENSE`;
[SameBoy API](https://github.com/LIJI32/SameBoy/wiki),
[Qt licensing](https://doc.qt.io/qt-6/licensing.html).

## 2026-10-05 — Inspection discovery and implemented contract

- An all-window saved-state test failed at `$FF00` using general
  `GB_safe_read_memory`. Source review confirmed PPU/APU reads can synchronize
  state. Replaced general peeks with public direct access, including raw IO
  storage. Only a literal FF50 mapping-flag read remains; its implementation
  does not synchronize a component. Full-address-space inspection is now pure.
- Actual opcode comes from the callback; operands are storage observations.
  This is verified for the fixed-ROM demo, not bus-conflict execution. Keep
  event times as atomic-step intervals, not exact write cycles.
- A test exposed retained/LCD-transition output at startup. First use now
  waits for two normal VBlank outputs after initialization; the star is visible.
- One emulator owner on Qt's main thread suffices: 3 ms quanta, 30 Hz state
  publication, bounded capture. A 300-frame traced run measured about 1 ms/frame
  here; an independent GUI timer continued firing. No worker/plugin framework
  is justified by this evidence.
- Keep write attempts only (4,096 default, configurable 8–65,536), with observed
  before/requested/after values. Capture toggling invalidates old writer claims.
  IRQ writes without an opcode callback have no instruction identity.
- Restrict loading to 32 KiB ROM-only DMG to make mapping/aliases explicit.
  General commercial compatibility, MBC, and automatic game metadata are deferred.
- Ubuntu packaging uses a `.deb` with OS Qt shared dependencies. Original
  ROM/boot/assembler/notices are included. The installed binary has no build
  RUNPATH and starts the embedded game from any directory. Clean-machine
  installation and a physical desktop compositor remain unverified.
- App/ROM/tool code is MIT; include SameBoy's Expat notice, Qt LGPL/GPL texts,
  and Ubuntu Qt copyright notices. Reference versions are locked in documentation;
  optional strict CMake mode requires Qt 6.4.2. Other Qt 6 versions are untested.
- Four CTest suites and native X11 widget interactions pass. Linux CI is added
  with checkout v4.2.2 pinned by commit; remote CI results are separate from local
  evidence. See `docs/VERIFICATION.md`. Independent Actions run 2 subsequently
  passed on a regular Ubuntu runner, confirming the ordinary dependency,
  native X11, build/test, and package-generation commands.
