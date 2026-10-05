# State and observation contract

## Ownership and time

`Engine` owns exactly one SameBoy DMG-B instance. It records its owning thread
and rejects other-thread access. Qt's main thread is also the emulator owner.
Live execution runs in 3 ms quanta, checked every 32 atomic core calls, using
wall time only for pacing. At most two frame periods of wall-time debt are kept.
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
accessed or patched. ROM/boot, VRAM, WRAM, OAM, HRAM, interrupt enable, and IO raw
storage are read directly. `$E000–$FDFF` aliases `$C000–$DDFF` in the DMG view.
No cartridge RAM or inaccessible OAM padding is presented as observed storage.
ROM bank 0/1 is fixed; no MBC support is promised by the adapter.

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
verify the position/OAM store and its visible consequence. Direct DMA OAM writes
and PPU accesses bypass this hook and are not captured. No source is fabricated.

Up to eight pending write records fit one atomic step (SM83 opcode/interrupt
work uses fewer in the tested slice). Overflow is counted as incomplete history.
Final records enter a fixed-capacity deque; eviction count and oldest retained
tick are published. Capture toggling clears history to prevent stale last-writer
claims after an unobserved interval. Inspection never contributes to activity
counters. Snapshots contain copies, not pointers into mutable core arrays.

The completed image is copied at each output callback and labeled with output
kind/frame number/enclosing boundary. It is not a reconstruction from current
OAM and it is not pixel provenance. A selected old write is evidence displayed
beside current exact state, never a claimed historical snapshot or replay.

## Responsibility boundaries

| Area | Owner |
| --- | --- |
| CPU execution, memory/hardware behavior, framebuffer production | unchanged SameBoy core |
| core lifecycle, pacing primitives, inspection, bounded event capture | `src/emulator/engine.*` |
| copied state/event structures and safe-byte disassembly | `src/emulator/state.hpp`, `disassembly.cpp` |
| semantic names and curated source notes | `src/teaching/annotations.*`, generated symbols |
| native controls, presentation, wall-clock scheduling, input focus | `src/ui/*` |
| reproducible original cartridge and boot | `rom/*`, `tools/assemble_rom.py` |

No plugin registry or generalized multi-console platform is needed for this
slice. Future hooks must specify their source, clock convention, and coverage
and must preserve tracing-on/off parity before new precision is advertised.
