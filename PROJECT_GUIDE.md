# Console Observatory: project guide

Working title. Living guide for people and AI contributors. Read alongside `AGENTS.md`, `README.md`, `STATUS.md`, and `DECISIONS.md`. A native Linux and Windows teaching build exists as of 2026-10-06; the broader goals below remain direction rather than claims of completed features.

## Current implemented slice

- Linux x86-64 and Windows x64, C++20, Qt 6 Widgets (tested 6.4.2 on Linux,
  6.8.3 MinGW on Windows CI, 6.4.2 MinGW cross build under Wine), CMake.
  SameBoy 1.0.3 DMG-B core is vendored at
  `208ba4afabffab9edde416f2dbb8ae459e34adb8`, with no upstream modifications.
- Original source-built teaching ROM, bank-switching demo (MBC1), and an
  original boot that leaves the documented DMG post-boot state are embedded.
  Arrow input updates named WRAM position variables and a visible star sprite.
  Normal use is offline and native, with no web modules/services.
- Any Game Boy ROM the user owns opens (up to 8 MiB, every controller SameBoy
  emulates), with battery `.sav` files. A Cartridge panel shows the CPU's
  windows, every ROM/RAM bank, the live mapping, per-bank activity, MBC
  register writes with their effect, and a plain-language explanation; the generic
  run-until-bank-change command stops at a real switch. User ROMs get no invented names.
- OAM DMA copies are observed (request, OAM before/after, match against the
  source) so sprite bytes in real games have an honest writer that leads back
  to the shadow table and its CPU writer.
- Every control and view explains itself in a tooltip card drawn from a
  plain-language glossary (registers, flags, memory regions and hardware
  registers, system parts, banks, tile bits and pixels, panels).
- Selection-driven learning replaces guided tutorials (0.6.0): 38 detailed
  linked general hardware articles, current observed facts, topic browsing,
  and Back/Forward history. Selection and reading do not advance execution;
  live updates preserve the article and reading position. Works with any ROM.
- Background/window map reconstructions link cells to VRAM entries and tile
  patterns, with unsigned/signed addressing resolved from current LCDC.
- Run/pause, one-opcode step, next-output frame step, run until a byte is
  written; registers/flags, storage disassembly, memory, VRAM/OAM/video
  register snapshots, changed-value highlighting, bounded write-attempt
  evidence, a sprite/tile bit-plane inspector, a per-address activity map,
  and a functional system overview. Panels dock and share one cursor.
- All state inspectors share the last atomic-step boundary. Completed display
  output has its own labeled frame and enclosing boundary. Activity summaries
  and the map have explicit intervals. Write callbacks identify attempts before
  acceptance; physical before/after values confirm the controlled demo.
- CTest checks full-state trace parity, run-until-write parity with untraced
  stepping, inspection purity, movement through WRAM/OAM to real pixels, tile
  decoding against source bytes and rendered colours, selection, reading-history, and arbitrary-ROM explanation
  checks, stepping, interrupt/HALT semantics, bounded growth, ROM
  reproducibility, and vendored integrity.
- Ubuntu `.deb` and a Windows portable ZIP (windeployqt) are produced and, on
  Windows CI, launched from a clean folder. A physical Windows desktop,
  installers, signing, and macOS remain unverified.

Next: an interrupt/timer event inspector linked to the reference, using real
IF/IE storage and execution evidence. Keep current-storage map reconstruction
distinct from historical pixel provenance; add keyboard navigation to painted views.

## Purpose

Make the connection between a running game and the computer executing it visible and understandable. Someone should be able to press a button, inspect the resulting instructions and memory changes, and explain how those changes become graphics or sound.

The audience includes curious beginners and people learning programming, emulation, or computer architecture. Provide a clear entry point and progressively deeper inspection. Technical detail should be available without overwhelming the default view.

The eventual ambition includes CPU, memory, graphics, sound, timers, interrupts, input, DMA, and cartridge hardware. Provide thorough explanations attached to meaningful selections. Keep general hardware reference distinct from currently implemented observation tools.

## Commitments and starting choices

**Commitments:**

- One integrated, standalone desktop application with a native interface. No browser UI, embedded web-app wrapper, local web server, account, or separate service required for normal use. Development tooling may fetch dependencies; the packaged teaching experience should work offline.
- Real emulation drives observed hardware views. Explicitly distinguish observations, summaries, annotations, and explanatory models.
- All inspectors refer to a shared emulated time cursor or an explicitly labeled interval.
- Prioritize understandable connections between events over decorative motion or a wall of debugger panels.

**Starting choices, open to revision:**

| Choice | Initial direction | Reason |
| --- | --- | --- |
| Console | Original monochrome Game Boy, DMG-B | Verified model available through SameBoy; memory and display support a coherent explanation. |
| Emulator | SameBoy 1.0.3 C core; unchanged, vendored commit pin | Existing opcode/write hooks support bounded instruction/store evidence without emulator patches. |
| Application | C++20, Qt 6 Widgets, CMake | Native resizable inspectors and direct C integration; no web wrapper. |
| Platforms | Ubuntu 24.04 Linux x86-64; Windows x64 with MinGW-w64 | Linux is the reference; Windows builds with Qt's MinGW kit because the core needs GNU C. macOS remains follow-up work. |
| Demo | Original ROM/boot, assembly source and strict Python subset assembler | Source-defined variables, no proprietary startup ROM, deterministic build without RGBDS. |

SameBoy documents a library build, debugger, and open-source replacement boot ROMs. Inspect its current APIs and licensing for the files actually used. Qt Widgets supplies desktop controls, model/view facilities, and a graphics-view framework. Verify the chosen Qt modules and distribution approach rather than assuming every dependency shares one license. See the primary references below.

## First working milestone

Deliver a real executable on the first platform that:

1. Loads a teaching ROM and displays the game with keyboard input.
2. Runs, pauses, steps one instruction, and advances one frame. Record the exact semantics of each operation.
3. Shows CPU registers, disassembly at the paused execution position, and selected memory bytes with changed values highlighted.
4. Associates a captured memory write with its executing instruction and shows a consistent state across inspectors.
5. Demonstrates Right input updating a named position variable and moving a sprite. Deeper inspection of sprite memory and transfer events can follow once basic tracing works.
6. Includes ROM source, dependency pins, build/run instructions, and verification results.

Make normal-speed execution usable with basic instrumentation. Detailed trace capture may be slower if its mode and limits are clear. Do not hold the first milestone for complete reverse execution, all hardware panels, installers for every OS, or polished gate simulation.

## Experience and visual language

Keep the game visible beside a compact hardware overview. Use sensible default sizes, resizable or dockable inspectors, keyboard controls, readable labels, and a recoverable default layout. Native does not mean plain: custom diagrams and carefully chosen animation are welcome.

Prefer related selection: selecting an address highlights its last captured writer; selecting a sprite reveals its record and tile; selecting an instruction reveals affected registers and addresses. Show plain-language names beside technical values when reliable annotations exist.

Memory has two distinct views: **contents** and **access activity**. Show byte values, changes, and region boundaries; use heatmaps for activity over a labeled interval. Account for bank switching and mirrored addresses so a displayed address is not mistaken for one permanent physical byte.

Use three timescales as the project develops:

- **Live:** aggregate activity over a labeled frame or interval. Keep the game playable.
- **Paused:** inspect an exact state and step supported units.
- **Captured:** replay a bounded event window around a selected instruction, address, or hardware event.

A component diagram is a functional schematic unless verified against actual circuitry. Mark simplified instruction stages or arithmetic models as explanations. Color should reinforce labels and changes, not carry meaning alone.

## Engineering boundaries

Separate the emulator adapter, trace/state model, native views, and teaching annotations. Use small interfaces that serve the first console; avoid designing a general emulator platform before its requirements exist.

Give emulator execution one owner. Publish immutable snapshots or otherwise synchronized copies to the interface. UI inspection must not race with emulation or cause hardware side effects. Verify debugger peeks rather than trusting their names; an inspection read must not become a simulated CPU read. The pinned `GB_safe_read_memory` can synchronize PPU/APU state in an IO window, so this slice uses public direct-storage access, with a tested literal-FF50 exception for boot mapping. IO is labeled raw storage rather than CPU bus readback.

Timestamp events using a documented emulated clock unit, including the core's timing convention. Record instruction identity, access kind, address, bank where applicable, and values needed by the view. Distinguish CPU access, DMA, PPU fetches, and debugger inspection where supported; ordinary CPU callbacks may not cover every source.

Keep trace capture bounded and configurable. Never send every emulated clock tick through the GUI event system. Start with frame summaries and a short event buffer. If records are dropped, indicate the incomplete interval. Treat checkpoint/replay or reversible logging as a later design decision for reliable rewind.

Keep upstream modifications minimal and documented. Confirm actual hook coverage and stepping granularity before naming features "cycle accurate." Make dependency acquisition and teaching-ROM generation reproducible. An ordinary user should eventually launch a packaged app without installing developer tools.

## Accuracy and learning limits

Graphics can change during a scanline. A future pixel inspector must capture the data used when a pixel was produced, rather than reconstruct it only from end-of-frame memory.

A last writer is evidence of a write, not a complete explanation of why it happened. Use symbols and curated annotations to connect observations to game concepts. For arbitrary ROMs, show supported facts without inventing names such as "player health" or claiming automatic complete causality.

Functional emulation, instruction-level explanations, cycle-level events, and physical gates are different depths. Support deeper views when justified, while labeling their fidelity and known gaps. Whole-hardware ambition does not require transistor simulation in the first version.

## Verification and handoff

Use the teaching ROM and relevant emulator test ROMs to check behavior. Compare execution with tracing enabled and disabled under the same controlled inputs. Check registers and memory as well as frame output: a correct-looking screen alone is insufficient.

Verify pause and stepping semantics, inspector consistency, keyboard focus, responsiveness, and bounded trace growth. Record tested platforms, dependency revisions, timing units, unsupported hooks, and any unverified behavior. Do not report a native GUI as verified if only a headless component ran.

Keep an updated README with exact build/run commands. Record meaningful decisions with their reason and evidence in `DECISIONS.md`; keep current status and the next concrete milestone in `STATUS.md`. Create these when implementation starts. Include attribution and dependency notices, and bundle an original or clearly redistributable teaching demo so first use needs no commercial game download.

## Growth without losing direction

Future possibilities include following a sprite or pixel, deeper tile/pixel inspection, interrupt and timer timelines, sound-channel exploration, editable values with immediate consequences, trace export, stronger replay, NES support, and selected logic-gate lessons. These are opportunities, not promises or a closed backlog.

For a new idea, identify the learning question, the real signal or model that supports it, and the smallest useful experiment. Prefer improvements that connect existing views and preserve playability. Revise a starting choice when an experiment reveals a better option; record the tradeoff. Make routine reversible refinements without a separate approval ritual. Preserve explicit user requirements unless the user changes them.

## Primary references

- [SameBoy source, build documentation, and features](https://github.com/LIJI32/SameBoy)
- [SameBoy license](https://github.com/LIJI32/SameBoy/blob/master/LICENSE)
- [Pan Docs: memory map](https://gbdev.io/pandocs/Memory_Map.html)
- [Pan Docs: graphics](https://gbdev.io/pandocs/Graphics.html)
- [Pan Docs: rendering and timing](https://gbdev.io/pandocs/Rendering.html)
- [Pan Docs: OAM DMA](https://gbdev.io/pandocs/OAM_DMA_Transfer)
- [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html)
- [Qt docking widgets](https://doc.qt.io/qt-6/qdockwidget.html)
- [Qt with CMake](https://doc.qt.io/qt-6/cmake-manual.html)
- [GateBoy / LogicBoy research](https://github.com/aappleby/metroboy), for later investigation of hardware depth

Verify the relevant upstream version when implementing; these links are research entry points, not guarantees of an integration already tested here.
