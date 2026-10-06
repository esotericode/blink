# State and observation contract

## Ownership and time

`Engine` owns exactly one SameBoy DMG-B instance. It records its owning thread
and rejects other-thread access. Qt's main thread is also the emulator owner.
Live execution runs in 3 ms quanta, checked every 32 atomic core calls, using
wall time only for pacing. At most two frame periods of wall-time debt are kept.
Nanoseconds are converted using whole seconds plus the remainder, avoiding
intermediate multiplication overflow during uninterrupted runs.
The core's timekeeping is disabled; no GUI event is emitted per instruction.
Immutable-by-convention `Snapshot` values are copied at publication boundaries
(roughly 30 Hz live, immediately on pause/step). There are no backend services.

`ticks` is the sum of `GB_run` return values since cartridge reset, in units of
1/8,388,608 second. `instructions` counts execution callbacks, not every atomic
call. The callback occurs after opcode fetch. Events are therefore tagged with
the enclosing **atomic-step start/end interval**, never an exact write cycle.
Interrupt service, HALT/STOP, LCD-off output, and artificial output have distinct
semantics. A frame callback may happen within an atomic step; its timestamp is
that step's completion boundary, not an exact callback clock.

Instruction step executes exactly one subsequent opcode, possibly after
interrupt service or waiting. The wait limit is two frame periods / 20,000
core calls. If no opcode occurs, the UI reports this and displays the actual
advanced state. Frame step reaches the next output callback, with a four-frame
/ 100,000-call guard. Views are published after the enclosing core call returns.

## Inspection

Use public `GB_get_registers` and `GB_get_direct_access`; no internal fields are
accessed or patched. ROM/boot, VRAM, WRAM, OAM, HRAM, interrupt enable, IO raw
storage, and cartridge RAM are read directly. `$E000–$FDFF` aliases
`$C000–$DDFF` in the DMG view. Inaccessible OAM padding, and `$A000–$BFFF` on
cartridges without RAM, are shown as no storage.

`GB_get_direct_access` also reports the bank mapped into each cartridge
window (`ROM0` at `$0000`, `ROM` at `$4000`, `CART_RAM` at `$A000`). `$0000–$7FFF`
reads ROM storage at `bank × $4000 + (address & $3FFF)`, or the boot program at
`$0000–$00FF` while it is mapped. `$A000–$BFFF` reads the selected RAM bank at
`((address & $1FFF) + bank × $2000) & (size − 1)`, matching SameBoy's read path.
That is storage, not a CPU read: the public API does not expose the MBC's RAM
enable or a selected clock/sensor register, so while RAM is disabled (or an
MBC3/HuC3/TPP1 clock register is selected) the CPU would read something else.
The UI states this. MMM01 rearranges ROM in emulator memory, so file offsets
derived from bank numbers are approximate for it.

One source-verified exception: `GB_safe_read_memory(gb, $FF50)` obtains the boot
mapping flag. The pinned implementation only returns `boot_rom_finished` with
fixed read mask; it does not synchronize a component. All other general safe
reads are avoided. The test visits every 128-byte address window and compares
full serialized state before/after. This matters because the pinned general
safe-read routine can call `GB_display_sync` or `GB_apu_read` through its map,
despite its "without side effects" comment. The initial IO-window test caught
this, and no upstream code change was needed to fix inspection.

The next disassembly decodes storage at PC, without simulating a fetch. An
execution record uses the actual opcode supplied by SameBoy plus operand
storage observed at that callback. Operand fetch events are not captured. For
DMA conflicts, unusual self-modification, bus blocking, and open-bus execution,
those operand observations are not guaranteed to equal actual fetched operands.
The controlled teaching ROM executes from fixed ROM, avoiding those conditions.

## Write evidence and fidelity

`GB_set_write_memory_callback` observes CPU write attempts before memory-map
acceptance. The observer always returns `true`. It stores instruction identity
from the current atomic call's execution callback. Identity is cleared before
every call: interrupt stack writes must never inherit the preceding opcode.

Before/after bytes are physical storage for WRAM/VRAM/OAM/HRAM, otherwise raw
register/ROM observations. IO masks and asynchronous peripheral effects are not
reconstructed. Equal requested/after values alone are not a general proof of
acceptance. The demo uses direct writes during VBlank, without DMA, and tests
verify the position/OAM store and its visible consequence. DMA's OAM writes and
PPU accesses bypass this hook; OAM DMA is observed separately (below). No source
is fabricated.

Up to eight pending write records fit one atomic step (SM83 opcode/interrupt
work uses fewer in the tested slice). Overflow is counted as incomplete history.
Final records enter a fixed-capacity deque; eviction count and oldest retained
tick are published. Capture toggling clears history to prevent stale last-writer
claims after an unobserved interval. Inspection never contributes to activity
counters. Snapshots contain copies, not pointers into mutable core arrays.
At equal ticks and unchanged capture mode, the UI retains the last published
activity interval while navigating memory; the engine's counters still reset
on each publication. The displayed interval keeps its original start/end times.

The completed image is copied at each output callback and labeled with output
kind/frame number/enclosing boundary. It is not a reconstruction from current
OAM and it is not pixel provenance. A selected old write is evidence displayed
beside current exact state, never a claimed historical snapshot or replay.

## Run until written

`Engine::runUntilWrite(address, limit)` repeats the same atomic `GB_run` calls
as stepping. After each call it checks that step's newly captured write
records for the canonical address (so `$E000` matches `$C000`). It stops at
that step's end boundary, so the cursor is the instruction boundary after the
writing instruction (or after interrupt service that pushed to the byte). It
returns the first matching record, the opcodes executed, outputs completed,
and ticks advanced. Without capture it refuses and does not advance. At the
limit (one emulated second from the UI, two to four frames in the lesson) it
stops at the first boundary past the limit. A test proves the full state
equals untraced instruction stepping by the same number of opcodes.

## Cartridge banks and battery RAM

The mapping (`BankMapping`: ROM bank at `$0000`, at `$4000`, RAM bank) is read
after every atomic step. A step that ends with a different mapping counts as
a ROM or RAM bank change; this is the same step granularity as all other
events, and the instruction that caused it is the controller write captured
in that step (when capture is on). Every write record carries the mapping
when the attempt was made and at the end of its step, and the bank of the
window it addressed. Writes to `$0000–$7FFF` are MBC commands on banked
cartridges and are ignored without an MBC; their "before/after" bytes are
ROM storage and are not presented as an effect.

`Engine::runUntilBankChange(limit)` repeats atomic steps until one changes the
mapping, then stops at that step's end boundary (one emulated second from the
UI, four frames in the bank lesson). It does not require capture. The
execution callback attributes each opcode start below `$8000` to the bank
mapped at its PC (boot-program opcodes are counted separately); the
instruction record carries that bank, so banked code is disassembled from the
right bank and labelled with it.

Battery-backed RAM (and clock state) uses SameBoy's `GB_save_battery_to_buffer`
/ `GB_load_battery_from_buffer` / `GB_get_battery_dirty`. Restart reloads the
same cartridge and restores battery RAM, like a power cycle, retaining any
unsaved status. Complete RAM and recognized clock footers are validated before
loading; malformed input changes neither the machine nor its dirty flag.
Short legacy RTC buffers receive full-union backing storage while retaining
their logical length, guarding the pinned loader's footer copy without vendor
changes. Export capability comes from the core's public save-size API.
The UI writes the
buffer to `<rom folder>/<rom name>.sav`; that is file I/O outside emulation and
does not change the emulated state.
An independent timer saves while paused. If saving fails on close/load, the
user chooses: save elsewhere, discard the unsaved progress, or cancel (the
default, which keeps the game). Rejected existing files are protected until an
explicit destination is chosen. The Cartridge panel displays persistent state/errors.
Writer lookup for cartridge RAM resolves `(bank × $2000 + offset) % RAM size`,
including small-RAM mirrors. Historical selections retain their recorded bank
beside the current mapping. Header and effective controller types are distinct
when the pinned core's padded-ROM, multicart, or RAM-recovery heuristics apply.

## OAM DMA

SameBoy's DMA is internal (no public hook or status). With capture on, the
write hook sees the CPU's write to `$FF46`; at that moment (before the core
accepts it) the engine copies OAM through direct access as the record's
`before`, with the requesting instruction and page. A copy already running is
closed as `Restarted`. After the requesting step ends, the engine waits at
least 1,296 ticks: SameBoy runs one warm-up machine cycle, 160 copies, and one
closing cycle (162 × 4 T-cycles × 2 ticks). Steps without an opcode (HALT,
interrupt service) extend the wait, because SameBoy pauses DMA while halted.
At the first boundary past that point it copies OAM as `after` and counts how
many of the 160 bytes equal their source byte read from storage (sources at
`$E000+` read `$C000+` on DMG, as SameBoy does). A test steps one instruction
at a time and confirms the last byte lands inside the window.

These are observations, not a per-byte trace: the record says when the copy
was requested and what OAM held before and after, not exactly when each byte
moved. Where the source changed during the copy, or OAM changed later, the
match count is below 160 and is shown. CPU stores to OAM made after the check
are newer writers than the copy. `runUntilWrite` on an OAM address also stops
at the boundary where a copy was checked. Records are bounded (64), cleared
with capture, and add nothing to the save state; tracing on/off parity is
tested with a per-frame DMA program.

## Video storage, previous output, and input

Each snapshot copies VRAM (8 KiB DMG), OAM (160 bytes), and LCDC, STAT, SCY,
SCX, LY, LYC, BGP, OBP0, OBP1, WY, WX raw storage through public direct access,
at the same boundary as the CPU state. Tile/OAM/palette decoding in
`emulator/graphics.*` is pure and operates only on these copies. Shade colours
are SameBoy's DMG palette mapping (shade n → `colors[3-n]`), so decoded tiles
and the picture use identical colours. Object colour 0 is shown as
transparent; the inspector does not model object priority or the 10-per-line
limit.

The vblank callback keeps the output it replaces as `previousPixels`, numbered
`previousFrame` (0 when none since reset). Changed-pixel marks compare those
two real outputs. Sprite outlines come from OAM at the CPU cursor, which can be
newer than the picture; that difference is the point of lesson step 3.

`heldButtons` reports the buttons the host is holding (keyboard or lesson),
not a joypad register read.

## Activity map

The engine counts, per 16-bit address, CPU write attempts by bus address (only
while capture is on; the write callback) and opcode starts by PC (the
execution callback). Counts cover `[startTicks, endTicks]` since the last clear;
load, restart, capture toggling, and the Clear button clear them. Copying the
map does not touch emulator state. It is not a read trace and does not include
DMA or PPU fetches, which the CPU hooks do not see.

## Responsibility boundaries

| Area | Owner |
| --- | --- |
| CPU execution, memory/hardware behavior, framebuffer production | unchanged SameBoy core |
| core lifecycle, pacing primitives, inspection, bounded event capture | `src/emulator/engine.*` |
| copied state/event structures and safe-byte disassembly | `src/emulator/state.hpp`, `disassembly.cpp` |
| cartridge header facts and MBC register names (no emulation) | `src/emulator/cartridge.*` |
| pure tile, OAM, and palette decoding of copied storage | `src/emulator/graphics.*` |
| semantic names, curated source notes, region names | `src/teaching/annotations.*`, generated symbols |
| guided lesson text from real evidence (button press, bank switch) | `src/teaching/lesson.*` |
| plain-language explanations shown as tooltips | `src/teaching/glossary.*` |
| native controls, presentation, wall-clock scheduling, input focus, tooltip card | `src/ui/*` |
| reproducible original cartridges (teaching, bank demo) and boot | `rom/*`, `tools/assemble_rom.py` |
| battery `.sav` files, ROM file loading | `src/ui/main_window.cpp` |

No plugin registry or generalized multi-console platform is needed for this
slice. Future hooks must specify their source, clock convention, and coverage
and must preserve tracing-on/off parity before new precision is advertised.
