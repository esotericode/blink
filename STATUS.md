# Status — 2026-10-06

Native desktop Game Boy exploration lab for Linux x86-64 and Windows x64.
C++20 / Qt 6 Widgets / CMake / unchanged SameBoy 1.0.3 DMG-B,
`208ba4afabffab9edde416f2dbb8ae459e34adb8`. Version **0.6.0**.

## Works

- **Selection info replaces the guided tutorials.** The most recent memory,
  CPU register, flag, sprite, tile, map-cell, system, bank, or captured-write
  selection opens a detailed general explanation. There are 38 linked topics
  covering memory and its regions, CPU/register/flag/stack basics, graphics,
  input, cartridge hardware, interrupts, timers, audio/serial roles, and the
  observation model. Topics have substantive mechanism, game-use, and
  inspector-reading sections. Unknown games receive no invented variables.
- Back/Forward reading history, topic browsing, related links, F1 restoration,
  selectable text, and a resizable/dockable reader. Live facts share the
  snapshot boundary; execution preserves the article and scroll position.
  Reading never advances or replaces the game. New sessions reset history.
- **Background/window map reconstruction** from copied VRAM and current LCDC/
  BGP: either active layer or either physical 32×32 map. Clicking a cell follows
  its map byte and resolved tile, including signed addressing. This is labeled
  current storage, not a reconstruction of historical pixel inputs.
- Original star and MBC1 bank-switching examples remain playable. There are
  no lesson buttons, progress stages, automatically held inputs, or guided
  lesson engine module. Exact ROM identity still gates source annotations.
- Any supported Game Boy ROM up to 8 MiB opens via File/drop. Cartridge panel
  shows effective controller facts, mapping, bank activity, and captured MBC
  commands. Memory and instruction evidence retain bank identity. Unmapped
  banks are explainable without forcing a hardware mapping change.
- Run/pause, one-opcode step, next-output step, run until written/bank change;
  copied synchronized CPU, memory, VRAM/OAM and register snapshots; bounded
  captured attempts; memory activity map; functional system diagram.
- Sprite animation follows current OAM; manual tile selection remains pinned.
  Tile patterns are decoded into bit planes, colour numbers, and palette shades.
  OAM DMA is observed as a request and later comparison, with qualified source
  evidence rather than per-byte provenance. Short explanatory tooltips remain.
- Battery `.sav` validation, protected rejected saves, atomic writes, paused
  autosaving, dirty-state preservation across restart, persistent status and
  retry/save-elsewhere/discard/cancel recovery remain from 0.5.0.
- Reproducible original ROMs and boot; vendor/dependency pins unchanged.
  Linux `.deb`, Windows portable ZIP and clean packaged launch; native CI,
  Qt-free AddressSanitizer checks; release notes in `docs/releases/v0.6.0.md`.
  A new version merged into main triggers the existing checked release pipeline;
  previously published versions are skipped.

## Verification

The reference catalogue, complete address routing, glossary keys, and vendored
hashes pass locally. Native Linux/X11 and Windows CI run the six CTest suites,
layout renders, package checks, and clean Windows launch. Exact results and
reviewed screenshot evidence are recorded in `docs/VERIFICATION.md`.

## Limits

DMG only; no CGB/SGB mode. Inspection uses copied storage, not synthesized
CPU bus reads. CPU write callbacks represent attempts before acceptance.
Activity counts do not include reads, PPU fetches, or DMA byte transfers.
Tile/map views use current storage and palette; they do not model a historical
viewport, raster effects, sprite priority, or per-pixel provenance. DMA capture
is bounded request/comparison evidence. Cartridge RAM enable and selected clock
register values are not exposed as inferred observations. No replay, save
states, sound output, audio wave inspector, linked-console session, or macOS
validation. Physical Windows/high-DPI testing, signing/installer support, and
clean-machine `.deb` installation remain follow-up validation.

## Next concrete milestone

An interrupt/timer event inspector linked to the existing hardware reference,
using actual requests and execution evidence. Add keyboard selection to painted
maps/tiles and validate the Windows package on a physical high-DPI desktop.

Read `PROJECT_GUIDE.md`, `DECISIONS.md`, `AGENTS.md`, and
`docs/TRACE_CONTRACT.md` before changing observation behavior.
