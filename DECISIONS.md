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
  (Superseded on 2026-10-06: any cartridge loads; see the last section.)
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

## 2026-10-06 — Any cartridge, with banks made visible

- The 32 KiB ROM-only limit was this project's own first-slice choice, not a
  SameBoy limit. Users wanted to open their own games, so it is lifted:
  `Engine::loadRom` accepts 0x150 bytes to 8 MiB (the largest MBC5 image),
  bounded before allocation. The header decoder in `emulator/cartridge.*`
  mirrors SameBoy's type table and its header/file-mismatch heuristics
  (no-MBC files over 32 KiB run as MBC3; MMM01 detected from the trailing
  header) so the UI describes what the core will actually do. MBC6, TAMA5,
  and unknown types are refused before the current game is replaced.
- Banks are inspected only through public `GB_get_direct_access`, whose bank
  output reports the bank mapped at `$0000`, `$4000`, and `$A000`. Inspection
  at `$0000–$7FFF` indexes ROM at `bank × $4000`; cartridge RAM uses
  SameBoy's own read-path indexing (`(a & $1FFF) + bank × $2000`, masked to the
  RAM size). The public API does not expose the RAM-enable latch or a selected
  clock register, so the view shows storage and says so, rather than guessing
  what a CPU read would return. The full-state purity test now also runs with
  bank 2 mapped and cartridge RAM present.
- Mapping changes are detected by comparing the mapping before and after each
  atomic `GB_run` step, which is the existing time granularity. Each write
  record carries the mapping at the attempt and at the step's end, so a write
  to `$2000` is shown as an MBC command with its effect (`ROM bank 1 → 2`)
  instead of ROM "before/after" bytes, which are meaningless there.
  `runUntilBankChange` stops at the end of the step that changed the mapping;
  it works with capture off, and with capture on it returns that step's
  controller write. Opcode starts are also counted per ROM bank, because the
  per-address map cannot distinguish banks at `$4000–$7FFF`.
- The original boot now leaves the documented DMG post-boot state (Pan Docs
  "Power Up Sequence": registers, LCDC = `$91`, BGP, sound registers, cleared
  VRAM; F depends on the header checksum). The previous minimal boot left the
  LCD off, and real games commonly wait for LY ≥ 144 before turning the LCD
  off, which never happens with the LCD already off. The teaching ROM's bytes
  and behaviour are unchanged; its warm-up still ends on a visible frame.
- Battery RAM uses SameBoy's buffer API, so `.sav` files are SameBoy's format
  (raw RAM, plus a clock footer for clock cartridges). Saves go next to the
  ROM with the same base name, written with `QSaveFile` (atomic replace) every
  ~3 s while the core reports them dirty, before another game is loaded, and
  on exit. The bundled bank demo saves under the per-user application data
  folder. Restart keeps battery RAM, as a power cycle would.
- Teaching value without inventing semantics: arbitrary ROMs get hardware
  facts (controller, banks, register names from Pan Docs) and real events,
  never variable names. A second original cartridge, `rom/bankdemo.asm`
  (MBC1, banks 1–3 each with a routine at `$4000`), gives a source-annotated
  bank lesson; it is identified by exact bytes like the teaching ROM. The
  assembler gained `BANK n`, generic encodings, and per-bank symbols.
- The machine stays DMG. CGB-only cartridges are allowed to load (they show
  their own "needs Color" screen on real DMG hardware too) and are labelled;
  CGB mode, SGB borders/palettes, and audio playback remain out of scope.
- UI: a "Cartridge · banks" dock (tabbed with Memory) holds a painted bank map
  (CPU windows → bank grid, connectors in the window colours), the
  explanation, counters, and the controller-write list. Rendered review at
  980×680 showed the panel could not fit, so it scrolls instead of
  overlapping; at the default size everything fits. Version 0.3.0.
- Windows (Wine) exposed a stack overflow: `Snapshot` carried two 92 KB
  frames inline, and the extended widget test plus the load path stacked ten
  of them against MinGW's 2 MiB main-thread stack. Frames are now heap-backed
  vectors with the same element access, so copies stay cheap on any stack;
  semantics and tests are unchanged. Per-step bank reads cost about 15% of
  traced emulation speed (still about 10× real time); accepted for now.
- Next: with real games loadable, sprite writes by OAM DMA are the largest gap
  in "who wrote this". The CPU's write to `$FF46` is already captured, so an
  honest "OAM copied by DMA from page $xx, requested at …" record is a small,
  verifiable step and is proposed ahead of the interrupt/timer lesson.

## 2026-10-06 — OAM DMA made visible; explanatory tooltips

- Real games fill OAM with DMA, so "who wrote this sprite byte?" had no answer.
  SameBoy keeps DMA state internal (`GB_is_dma_active` is not public), and
  vendored files stay unmodified, so DMA is *observed*: the write hook already
  sees the CPU's `$FF46` write; at that moment OAM is copied as "before". The
  completion check waits 162 machine cycles after the requesting step, the
  duration read from `GB_dma_run` (warm-up, 160 copies, closing cycle), and
  longer across steps without an opcode, because SameBoy pauses DMA while the
  CPU is halted. A test steps one instruction at a time and confirms SameBoy's
  last OAM byte lands inside that window; tracing on/off parity holds.
- The record reports how many OAM bytes equal their source at the check rather
  than claiming a per-byte transfer, and a restarted copy is labelled as such.
  The UI makes the DMA OAM's last writer only when it is newer than the last
  captured CPU store, and links the source byte so the chain sprite ← DMA ←
  shadow table ← CPU store can be followed by clicking.
- A hand-assembled fixture (`tests/fixtures.hpp`) uses the standard pattern —
  routine copied to HRAM, `LDH [$46], A`, busy-wait — because on DMG the CPU
  cannot fetch from ROM during the copy. No new bundled ROM was needed.
- Tooltips: Qt's plain tooltip cannot show a structured explanation, and its
  stylesheet cannot round or shadow it reliably. An application event filter
  answers every tooltip request (static tooltips, item-view items, headers,
  menu actions, dock tabs and title bars) with one card: an accent-coloured
  title, the explanation, and a hint in a footer band. Custom-painted views
  answer for the region under the pointer (a diagram part or arrow, a bank, a
  tile, one bit or pixel of a tile row, a memory-map cell). After one tip, moving
  to something else explains it immediately, as Qt's own tooltips do.
- The card is opaque, rounded by a window mask, on every platform. A
  translucent variant with a soft shadow showed black margins on X11 without
  a compositor and did not appear at all under Wine, and it could not be
  checked on a real Windows desktop, so it is opt-in only
  (`OBSERVATORY_TOOLTIPS=soft`). A tooltip that never appears would be worse
  than one without a shadow.
- Text lives in a Qt-free glossary (`src/teaching/glossary.*`), so wording is
  reviewed in one place and kept consistent; a CTest suite rejects unknown keys
  and over-long entries. Facts follow Pan Docs; writing aimed at a smart
  newcomer (each term explained where it is used). Review corrected two drafts
  (instructions per frame, sprite overlap priority) before publishing.
- Version 0.4.0.

## 2026-10-06 — GitHub releases

- Releases are built by CI, not uploaded from a developer machine: the Release
  workflow checks out the exact commit, runs the same suites as CI (including
  the native-platform widget suite and, on Windows, a launch of the unzipped
  package with only System32 on `PATH`), and only then attaches the files. A
  draft is created first and published last, so a failed build never leaves
  a half-filled public release. `SHA256SUMS.txt` is computed from the attached
  files.
- The tag must match `project(... VERSION)` at that commit, so file names,
  About box, and tag agree. Notes live in the repository
  (`docs/releases/<tag>.md`) and are reviewed like code.
- Published: v0.2.0 (first Windows build), v0.3.0 (any ROM, banks), and
  v0.4.0 (DMA, tooltips; latest), each built from the last commit of that version.
  v0.1.0 was Linux-only and predates the packaging the workflow expects, so
  it has no release. Older notes say they are superseded.

## 2026-10-06 — Save safety and explanation consistency

- Validate ROM controller support in Engine before replacing state. Battery
  buffers must hold complete RAM and a recognized footer; rejection changes
  neither state nor the dirty flag. The pinned loader copies a full RTC union
  using the total buffer length, so recognized shorter legacy RTC buffers are
  backed by a padded allocation while retaining their original logical size.
  Vendor source remains byte-for-byte upstream.
- Use the core's public battery-size API for export capability. Decode the
  same padded ROM, trailing MMM01, ROM-only fallback, and RAM-recovery order
  for displayed effective controller facts; retain the declared header type
  and explain disagreement.
- Failed disk saving retains the engine. A rejected existing save is protected
  until the user explicitly chooses a destination. An independent Qt timer
  saves dirty RAM while paused; restart carries unsaved battery status.
- Last retained CPU attempts resolve banked/mirrored cartridge RAM storage.
  Historical selections name the recorded bank separately from the current
  mapping. DMA language describes requests and later comparisons, never
  inferring per-byte provenance or excluding an unobserved DMA request.
- External execution, input, focus loss, and capture changes leave the active
  lesson. Starting the movement lesson resets to a known completed frame;
  each store/output is checked before the next success page. Add short
  prediction/explanation prompts and plain-language register-to-variable moves.
- Preserve the last activity interval during same-tick UI browsing, follow
  animated sprite tiles, pin manually chosen tiles, reveal linked Memory,
  and label picture age relative to current state. Native widget regressions
  exercise these interactions alongside save failure and recovery.
