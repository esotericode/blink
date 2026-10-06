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

## 2026-10-06 — Windows as a second platform

- Toolchain: **MinGW-w64 GCC**, from Qt's own installer kit. SameBoy's core
  needs GNU C (case ranges, statement expressions, attributes); MSVC `cl.exe`
  cannot compile it, so CMake stops with an explanation instead of failing
  deep in the core. clang-cl is not tested. MSYS2's Qt was not chosen because
  windeployqt does not collect the other MSYS2 DLLs that build depends on.
- Evidence the core is sound on Windows' LLP64/MS-bitfield ABI: a MinGW cross
  build passed the engine suite under Wine (full save-state parity, stepping,
  movement, purity). The one compiler warning (`fseek(f, -sizeof(magic), ...)`)
  truncates to the intended -4 and is in an unused file-based API.
- Packaging: a portable ZIP whose root holds the `.exe`, Qt DLLs, plugins,
  MinGW runtime, and `qt.conf` (`CMAKE_INSTALL_BINDIR="."`), produced by
  `qt_generate_deploy_app_script` (6.4 and 6.5+ signatures). A relative
  `--prefix` is resolved before Qt 6.5+'s deployment, which requires an
  absolute path (found by Windows CI). No installer or signing yet.
- CI uses official Qt 6.8.3 MinGW + MinGW 13.1 binaries; local verification
  used Qt 6.4.2 qtbase cross-built from source because this environment's
  network policy blocks download.qt.io. Both the oldest supported and a
  current LTS Qt are therefore exercised.
- Findings from the first Windows runs, all fixed: Qt ≥ 6.5 `findChild<T>()`
  requires `Q_OBJECT` in T (affects every newer Qt, not only Windows); Git for
  Windows' autocrlf changed vendored bytes (now pinned LF by `.gitattributes`);
  relative deploy prefixes. Windows-specific polish: GUI subsystem, icon and
  version resources, Consolas for code, no forced Linux font, and game
  scaling by whole device pixels for fractional DPI.

## 2026-10-06 — Visual lessons on the same evidence model

- The next milestone (pause on the `player_x` store, follow the OAM store, see
  the frame, inspect the tile) is a guided lesson. Each stop is a real event:
  `Engine::runUntilWrite` stops at the end of the atomic step containing the
  write attempt. A test proves it reaches byte-identical full state to
  untraced instruction stepping, so the lesson does not perturb emulation.
- Lesson text lives in `src/teaching/lesson.*` (no Qt) and is filled from the
  stopping event and measurements. A widget test asserts the claims it makes,
  e.g. that the picture is unchanged until the frame step and the reported
  changed-pixel count. Lesson-enabled overlays reset on stop/load/restart:
  comparing across a reset marked the whole screen as changed.
- Snapshots now copy VRAM (8 KiB), OAM, and video registers through public
  direct access; the all-address-space purity test still passes. The previous
  completed output is retained by the vblank callback, so change marks compare
  two real outputs rather than a reconstruction.
- Tile colours come from SameBoy's own DMG palette mapping (shade n →
  `colors[3-n]`); a test checks every rendered pixel is one of those colours.
- The memory map counts per-address CPU write attempts (capture on) and opcode
  starts since its last clear, labelled with its tick interval. It is not a
  read or bus trace. Single busy bytes get a minimum 3 px marker so a one-byte
  variable is visible at one screen pixel per address.
- Layout: dockable panels (QDockWidget) replace fixed splitters because seven
  panels no longer fit side by side; the default layout is rebuilt in code
  and recoverable. Sizes were chosen from rendered screenshots: the game keeps
  3× at 1280×930 and 2× at the new 980×680 minimum, which fits 1366×768
  laptops. Layout is not persisted between sessions yet.
- Enter on a focused button now activates it instead of becoming Game Boy
  Start; elsewhere Enter remains Start.
