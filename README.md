# Console Observatory

A native desktop Game Boy teaching lab. Run an original tiny game, press Right,
and follow the change from a CPU instruction to `player_x`, sprite memory, and
the next displayed frame. C++20 / Qt 6 Widgets / SameBoy; Linux first.

The application starts paused with its bundled game already visible. Normal
use works offline in one executable with ordinary Qt runtime libraries. No
browser, local server, account, backend, commercial game, or proprietary boot
ROM is required.

## Build and run — Ubuntu 24.04 x86-64

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build python3 qt6-base-dev qt6-base-dev-tools
git clone --branch console-observatory-v1 https://github.com/esotericode/blink.git
cd blink
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/console-observatory
```

Tested: GCC 13.3.0, Qt 6.4.2, CMake 3.28.3, Ninja 1.11.1, Python 3.12.3.
Minimums: CMake 3.22, Python 3.8, Qt 6.4, a GNU-compatible C11 compiler and
C++20 compiler. SameBoy's exact source is vendored, so configuration/build do
not fetch dependencies. See `DEPENDENCIES.lock.json` for the reference inputs.
Use `-DOBSERVATORY_STRICT_DEPENDENCIES=ON` to require the reference Qt 6.4.2;
compatible newer Qt 6 is otherwise allowed and must be verified.

For Qt installed in a nonstandard prefix, add
`-DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64`. On a machine without a desktop,
CTest's widget suite uses Qt's offscreen plugin. Test X11 separately with:

```bash
sudo apt install xvfb xauth
xvfb-run -a ./build/widget_tests
```

## Try the lesson

1. Click **Run** (F5), hold **Right** briefly, and click **Pause** (F5).
2. Click **Inspect player_x and its captured writer**. The byte at `$C000` is
   selected, and its last retained write identifies `LD [$C000], A`.
3. Inspect the before/requested/after values and source annotation. Select an
   OAM X write in the captured list to select `$FE01` in memory.
4. Use **Instruction** (F10) to execute one opcode; gold values marked `Δ`
   changed since the preceding published snapshot. Use **Frame** (F11) to reach
   the next completed display output.

Arrows move horizontally/vertically. Z/X, Backspace, and Enter map to Game Boy
A/B/Select/Start; this game only uses directions. Game keys work while tables
have focus. Editing a memory address uses normal text controls. Losing window
focus releases held buttons. **Reset layout** restores the resizable panels.

`player_x`/`player_y` are screen coordinates at `$C000`/`$C001`; `$C002` stores
polled buttons and `$C003` counts game updates modulo 256. Sprite 0's X/Y bytes
are at `$FE01`/`$FE00`: OAM X is `player_x + 8`, OAM Y is `player_y + 16`.
These names are source annotations, enabled only when ROM bytes exactly match
the bundled demo. The program polls once per VBlank and clamps movement to the
160×144 display. This is a teaching game, not a commercial title.

## ROM source and rebuilding

```bash
python3 tools/assemble_rom.py --source rom --output out/rom
./build/console-observatory out/rom/teaching.gb
```

`rom/teaching.asm` contains all game code and original tile/sprite data.
`rom/boot.asm` is an original 256-byte teaching boot: it disables boot mapping
and transfers to cartridge `$0100`. It does not reproduce hardware startup or
validate headers. No Nintendo logo is embedded. The strict, two-pass Python
assembler supports the subset actually used, rejects unsupported syntax and
overlaps, resolves symbols, and writes checksums. No RGBDS is required.

Outputs: `teaching.gb`, `boot.bin`, `teaching.sym`, `manifest.json`, and the
generated embedding header. CMake runs the same assembler and embeds both
binaries into the executable. The reference teaching-ROM SHA-256 is
`e7ef9ff22686174b0ccfabc86e44fd3fcf65561f486cb4f236cb674a69d9d672`.
Editing the ROM rebuilds its embedding and source-defined annotation addresses.

## Install or create a Linux package

```bash
cmake --install build --prefix "$PWD/out/install"
./out/install/bin/console-observatory
cpack --config build/CPackConfig.cmake -B out
sudo apt install ./out/console-observatory_0.1.0_amd64.deb
console-observatory
```

The Ubuntu 24.04 `.deb` includes a desktop launcher, icon, the original ROM
source/binaries, assembler, and license notices. It uses dynamically linked OS
Qt packages. Developer tools are needed only to build/rebuild; the installed
app needs the Qt runtime. Cross-distribution installers, AppImage, macOS and
Windows builds are not verified yet. Build without verification executables
with `-DBUILD_TESTING=OFF` if needed.

## State, tracing, and exact limits

- CPU registers, next instruction storage bytes, memory contents, OAM, and
  hardware register storage are copied at one instruction boundary. A paused
  inspector never advances emulation. IO values are **raw core storage**, not
  synthesized CPU bus reads; absent storage shows `—`.
- The screen is the latest completed core output, with its own frame number,
  output kind, and enclosing instruction-boundary tick. It stays unchanged
  while stepping instructions until another completed output arrives. Pixel
  contents may represent earlier work than the CPU cursor.
- Instruction step advances through any pending interrupt/wait work until one
  opcode executes. A bounded HALT/STOP wait reports no opcode and shows the
  time actually advanced. Frame step stops after the next output callback's
  enclosing atomic core step. LCD-off/artificial output is labeled.
- Live activity summarizes a labeled interval between published snapshots.
  It counts opcodes and write attempts, not reads, PPU fetches, or bus cycles.
  Write events record the enclosing interval in SameBoy's 8,388,608 Hz ticks,
  executing opcode where available, address/bank, requested data, and observed
  storage before/after. A callback reports an **attempt**, not acceptance.
- Default history: 4,096 write records. `--trace-capacity 256` selects a smaller
  window (8–65,536). Evictions and incomplete earlier history are displayed.
  Turning capture off/on clears stale writer evidence. Only the latest 24
  attempts appear in the list; memory selection searches the whole retained
  window. Selecting historical evidence does not rewind the current state.
- This adapter accepts 32 KiB ROM-only monochrome DMG cartridges. WRAM echo
  addresses are canonicalized; bank switching is deliberately excluded.
  The minimal boot is for teaching code. General commercial-game compatibility
  is not claimed. Audio hardware is emulated but playback is not implemented.
- No read trace, DMA/PPU transfer trace, exact write-cycle timestamp, reverse
  execution, pixel provenance, gate model, or automatic complete causality.
  Before/after storage confirms the demo's writes; it does not identify every
  possible hardware source for arbitrary code.

## Verification and next milestone

Four CTest suites pass: engine behavior, rendered native Qt widgets, deterministic
ROM generation, and vendored source integrity. The engine check compares full
SameBoy save-state bytes, registers, WRAM, OAM, frame pixels, ticks, and opcode
counts with tracing enabled/disabled for 90 controlled frames; verifies a real
one-pixel movement; inspects the full address space without state changes; and
checks bounded history, interrupt attribution, HALT, and step synchronization.
Qt controls were rendered/tested under both offscreen and X11 virtual displays.
Physical desktop/compositor and other OSes remain unverified. See
`docs/VERIFICATION.md` for exact evidence and performance limits.

Next useful milestone: a bounded pause-on-write lesson that stops at the
`player_x` store, follows the subsequent OAM store, then advances to its visible
frame; add a sprite/tile bitplane inspector using real storage. Add verified
DMA instrumentation before expanding that lesson to DMA-based games.

Read `PROJECT_GUIDE.md`, `AGENTS.md`, `STATUS.md`, `DECISIONS.md`, and
`docs/TRACE_CONTRACT.md` before contributing. Third-party licenses and exact
inputs are in `THIRD_PARTY_NOTICES.md` and `third_party/sameboy/UPSTREAM.md`.
