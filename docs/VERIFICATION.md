# Verification

These results cover this implementation and controlled teaching ROM, not all
SameBoy-supported games or desktop platforms. Newest first.

## 2026-10-06 — Observed OAM DMA, explanatory tooltips (0.4.0)

| Check | Evidence / result |
| --- | --- |
| Build | Clean Release build on Linux; no warnings from project code (one GCC dangling-reference false positive fixed by copying the glossary entry). |
| CTest | **5/5**: engine, widgets offscreen, ROM reproducibility, vendor integrity, and the new glossary key check (88 entries, 85 keys used by the interface, all present, longest explanation 218 characters). |
| Engine | **Twelve groups**. New OAM DMA group on an original HRAM-routine fixture: the `$FF46` write is captured from `$FF82`; while copying, the snapshot publishes the pending record with OAM "before"; stepping one instruction at a time, SameBoy's last OAM byte arrives more than 1,200 and at most 1,296 ticks after the requesting step (inside the checked window); the check lands 1,296–1,359 ticks after it with 160/160 bytes equal to the source and OAM = 0…159; changed-byte count matches; run-until-written on `$FE01` stops at the next frame's checked copy (sprite 0 X 1 → 2, one byte changed); a double request records `Restarted` then `Checked`; 12 frames of per-frame DMA give identical full save state with tracing on and off, and no records with capture off. |
| Widgets | DMA fixture loaded from a file: F9 on `$FE01` stops at the first and second copies; writer text names OAM DMA, 160 of 160, and links `$C101`; following the link selects `$C101`, whose writer is `INC [HL]`; the Sprites panel's OAM source line names `$C100`; the `$FF46` row says "starts OAM DMA from $C100". Tooltips on real controls: the PC register cell, the Z flag chip, the CPU part of the system overview, a memory byte (`$C000 player_x`, WRAM, value), the Cartridge dock tab, the Run toolbar button, the bank map's fixed window, and one pixel of the tile detail ("colour number"); the card is at most 400 px wide and hides on request. |
| X11 | Widget suite under `xvfb-run` (1600×1000 screen) with screen grabs of tooltip cards: register, diagram part, memory byte (with hint footer), tile pixel. Found and fixed: dock tabs carry Qt's own title tooltip, which hid the panel explanation; the diagram's DMA label was clipped by the PPU box (moved into the OAM box). The soft (translucent) style shows black margins without a compositor, so X11 defaults to the flat style. |
| Windows (Wine 9.0) | MinGW cross build: engine suite (twelve groups, including the DMA timing check) and widget suite (including tooltips) pass with the native `windows` plugin. **Found and fixed:** the translucent card, then the Windows default, never appeared in Wine's screen grabs, while the opaque card did (screenshot inspected). The opaque card is now the default on every platform; the translucent one is opt-in (`OBSERVATORY_TOOLTIPS=soft`) because it could not be checked on a real Windows desktop. |
| GitHub Actions | Commit `83192a5`: [Linux run 15](https://github.com/esotericode/blink/actions/runs/37415935064) and [Windows run 11](https://github.com/esotericode/blink/actions/runs/37415935062) (official Qt 6.8.3 MinGW: all suites, native-platform widget suite with tooltips, windeployqt package, clean-PATH launch) succeeded; so did runs 14 and 10 for `f198838`. |

## 2026-10-06 — Any cartridge, bank visualisation (0.3.0)

### Linux (reference: Ubuntu 24.04, GCC 13.3.0, Qt 6.4.2)

| Check | Evidence / result |
| --- | --- |
| Build | Clean Release build; no warnings from project code. |
| CTest | **4/4** (engine, widgets offscreen, ROM reproducibility, vendor integrity). |
| ROMs | `rom_tests.py` rebuilds all outputs byte-identically; checks both headers/checksums and the bank demo's layout (each bank starts with its own routine, distinct patterns). Teaching ROM SHA-256 unchanged; new boot `e758c75f…895f`, bank demo `b9c2530b…8c7`. |
| Engine | **Eleven groups** pass. New: post-boot hand-over (AF `$01B0`/`$0180` by header checksum, BC/DE/HL/SP, LCDC `$91`, BGP, FF50, cleared VRAM). MBC1 demo: header decode; `$4000` window equals bank 1, then bank 2 storage; `runUntilBankChange` stops with the controller write (`$01D9 LD [$2000], A`, 1 → 2) and PC at the following `CALL`; the next opcode at `$4000` is bank 2's and is attributed to bank 2; cartridge RAM write before/after; banks 3 → 1 → 2; per-bank opcode counts sum with boot opcodes to all cartridge opcodes; **full-state purity with bank 2 mapped and RAM present**; battery bytes, restart keeps them, a fresh engine loads them. Banked **trace on/off parity** (three switches, full save state). MBC5 at 128 KiB switches; ROM size bounds rejected without touching state; header heuristics (no-MBC > 32 KiB → MBC3, unknown/MBC6 unsupported, CGB flags, clock). Existing groups unchanged; fixtures now enter the cartridge after the boot hand-over. |
| Widgets | Both lessons through their buttons. Bank lesson: stops with bank 2 mapped and PC at `CALL $4000`; text contains the real writer and `1</b> to <b>2`; switch list row `$2000 ← $02` / `ROM bank 1 → 2`; the Memory panel's `$4000` window follows to bank 2's bytes; CPU panel shows `$4000 bank 2` after one step; MBC writer text; second press → bank 3; the panel's run button stops at its limit without a press. Battery `.sav` written (8,192 bytes, signature and count). An MBC5 ROM opened by **drag and drop** loads its existing `.sav`, starts paused at power-on, shows its facts and file offsets, switches banks on a held key via the panel button, and its `.sav` is updated when another game is opened. No register "Δ" against the previous game. Cartridge panel for the no-MBC teaching ROM; diagram click opens it. |
| X11 | Widget suite under `xvfb-run` with screenshots of every lesson stage and tab, including `tab-cartridge-teaching`, `bank-0-start`, `bank-1-switched`, `tab-memory-banked`, and `minimum-cartridge`. |
| Layout review | Found and fixed: Cartridge panel overlapped itself at 980×680 (now scrolls; fits at the default size); truncated controller-write column (resizing + tooltips); bank demo opening on its blank first LCD frame (warm-up waits for two visible frames); registers marked changed against the previous game after a load; screenshots taken before pending layouts ran (tests now process events first). |
| Performance | Traced sustained run: 1.49–1.84 ms per emulated frame across three runs, versus 1.31–1.57 ms for the previous commit on the same machine (per-step bank reads and per-opcode bank attribution); about 10× faster than real time. |

### Windows (local cross build)

| Check | Evidence / result |
| --- | --- |
| Build | Ubuntu MinGW-w64 GCC 13 against Qt 6.4.2 qtbase built from source: app, `engine_tests.exe`, `widget_tests.exe`. Only the known vendored `save_state.c:1500` warning. |
| Engine (Wine 9.0) | All eleven groups pass. |
| Widgets (Wine 9.0, native `windows` plugin) | Passes, with all screenshots written and inspected. **Found and fixed:** the extended suite crashed with a stack overflow, because each `Snapshot` held two 92 KB frames inline (about 193 KB) and MinGW's main thread has a 2 MiB stack; the app's load path also stacked several snapshot temporaries. Frames are now heap-backed vectors (always 23,040 pixels), making a snapshot about 9 KB. |

### GitHub Actions (commit `c2ad784`)

| Workflow | Result |
| --- | --- |
| [Linux run 11](https://github.com/esotericode/blink/actions/runs/37412613635) | Success: build, 4/4 CTest, X11 widget suite, package. |
| [Windows run 7](https://github.com/esotericode/blink/actions/runs/37412613633) | Success with official Qt 6.8.3 MinGW: build, 4/4 CTest, widget suite on the native `windows` platform (both lessons, drag-and-drop MBC5 load, battery files), windeployqt install, CPack ZIP, and launch of the unzipped package with only System32 on `PATH`. Artifact (ZIP + screenshots): 13,724,836 bytes. |

## 2026-10-06 — Windows build, guided lesson, visual inspectors

### Linux (reference: Ubuntu 24.04, GCC 13.3.0, Qt 6.4.2, strict mode)

| Check | Evidence / result |
| --- | --- |
| Build | Clean Release build; no warnings from project code with `-Wall -Wextra -Wpedantic`. |
| CTest | **4/4** (engine, widgets offscreen, ROM reproducibility, vendor integrity). |
| Engine | Nine groups pass. New: tile/bit-plane/palette/OAM decoding against the source tile bytes; every rendered pixel is one of the inspector's shade colours and the dark-pixel count equals the decoded star; run-until-write limit, `player_x` store boundary, OAM X store before any new frame, next frame +1 px, echo-address match, capture-off refusal; run-until-write reaches the **identical full save state** as untraced instruction stepping (three watches); activity-map interval and per-address counts (10 frames → 10 writes to `$C000`, `$FE01`, `$FF00`). Trace parity now also compares VRAM, OAM, and the previous output. |
| Purity | The all-address-space inspection test still leaves full serialized state unchanged with the new VRAM/OAM/video-register copies. |
| Widgets | The suite drives the lesson through its buttons and asserts at every stop: hold without running; stop with PC = writer + 3, `player_x` +1, **same frame and pixels** as before (the lesson's claim); OAM X = `player_x` + 8 with no new frame; one new output with 1–63 changed pixels and that count in the text; sprite 0 / tile 2 selected; release. Also F9 on `frame_counter`, writer disassembly in the list, map/memory linking, map Clear, and the unannotated-ROM lesson state. |
| X11 | Widget suite under `xvfb-run` at 1600×1000 and at the default 640×480 screen (window clamps to the 980×680 minimum). |
| Layout | Screenshots of the default and minimum sizes and of every lesson stage and tab inspected. Game at 3× (1280×930) and 2× (980×680). Findings fixed during review: clipped diagram text, unstyled checked buttons, `&` mnemonic in a tab title, game overlapping its controls at minimum size, clipped bold button text, a stale changed-pixel overlay after reload, a 1× tile sheet. |
| Package | Install tree and `console-observatory_0.2.0_amd64.deb` generated. |

### Windows

| Check | Evidence / result |
| --- | --- |
| Core on Windows ABI | Ubuntu MinGW-w64 GCC 13 (posix, SEH) compiled all 15 SameBoy sources. One warning, `save_state.c:1500` `fseek(f, -sizeof(magic), …)`, truncates to the intended −4 under LLP64 and is in an unused file API. |
| Local cross build | Qt 6.4.2 qtbase cross-built from the GitHub source tag with the same toolchain (host tools from Ubuntu's Qt 6.4.2). App, `engine_tests.exe`, and `widget_tests.exe` built in strict mode. |
| Local run (Wine 9.0) | Engine: all nine groups pass. Widgets: pass with Qt's native **`windows`** platform plugin on a virtual X display, including the full lesson; stage screenshots inspected (fonts are Wine substitutes). |
| Local package layout | ZIP generated by CPack. With the Qt/MinGW DLLs, `qwindows.dll`, and `qt.conf` placed as windeployqt would (windeployqt is not built in a cross build), the `.exe` launched from an unrelated folder with an empty `WINEPATH` and rendered `--screenshot`. The `.exe` is GUI-subsystem, imports only Qt, MinGW runtime, and system DLLs, and embeds its icon and version resources. |
| GitHub Actions | `windows-2025`, official **Qt 6.8.3 MinGW** and **MinGW 13.1** via install-qt-action. [Run 4](https://github.com/esotericode/blink/actions/runs/37395750904) (commit `703f80a`, all features) and [run 5](https://github.com/esotericode/blink/actions/runs/37396085975) (commit `2829ff1`, trimmed deployment): build, **4/4 CTest** (widgets offscreen), widget suite on the native `windows` platform (all lesson stages), `cmake --install` with windeployqt from a relative prefix, CPack ZIP, and launch of the **unzipped package with only System32 on PATH** and `QT_PLUGIN_PATH` removed, rendering its screenshot. Artifacts hold the ZIP and screenshots. |
| Windows package | Run 5 ZIP: 12,350,849 bytes (run 4: 23,452,231 before trimming). Contents: `console-observatory.exe`, `Qt6Core/Gui/Widgets.dll`, `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`, `qt.conf`, `plugins/platforms/qwindows.dll`, `plugins/styles/qmodernwindowsstyle.dll`, README, notices, `licenses/`, `rom/`, `tools/assemble_rom.py`. windeployqt reported dependencies Qt6Core, Qt6Gui, Qt6Widgets only. |
| Findings from Windows CI, fixed | Qt ≥ 6.5 `findChild<T>()` requires `Q_OBJECT` (affects every newer Qt); Git for Windows autocrlf changed vendored bytes (LF pinned in `.gitattributes`); Qt ≥ 6.5 deployment needs an absolute prefix (resolved in CMake); 23 MB package with unused OpenGL/network/SVG parts (trimmed on Qt ≥ 6.7). |

Not verified: a physical Windows 10/11 desktop and monitor, real fractional
DPI, keyboard layouts other than US, an installer or code signing, and Qt 6.5–6.6
on Windows (deployment branch written to the documented 6.5 API but untested).

### Reproduce the Windows cross build (Linux host)

```bash
sudo apt install g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix wine64 qt6-base-dev qt6-base-dev-tools xvfb
git clone --depth 1 --branch v6.4.2 https://github.com/qt/qtbase qtbase-src
# toolchain file: CMAKE_SYSTEM_NAME Windows, x86_64-w64-mingw32-{gcc,g++}-posix, windres
cmake -S qtbase-src -B qtbase-build -G Ninja -DCMAKE_TOOLCHAIN_FILE=mingw.cmake \
  -DQT_HOST_PATH=/usr -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/x86_64-linux-gnu/cmake \
  -DCMAKE_INSTALL_PREFIX=$PWD/qt6-mingw -DBUILD_SHARED_LIBS=ON -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
  -DFEATURE_sql=OFF -DFEATURE_network=OFF -DFEATURE_dbus=OFF -DFEATURE_opengl=OFF -DINPUT_opengl=no
cmake --build qtbase-build && cmake --install qtbase-build
cmake -S blink -B win -G Ninja -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DQT_HOST_PATH=/usr \
  -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/x86_64-linux-gnu/cmake -DCMAKE_PREFIX_PATH=$PWD/qt6-mingw \
  -DOBSERVATORY_DEPLOY_QT=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build win
WINEPATH="<qt6-mingw\bin>;<mingw runtime dirs>" wine win/engine_tests.exe
QT_QPA_PLATFORM=windows xvfb-run -a wine win/widget_tests.exe
```

## 2026-10-05 — First Linux slice

### Reference build

Ubuntu 24.04 x86-64, GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, Python 3.12.3,
Qt 6.4.2 (`6.4.2+dfsg-21.1build5`). SameBoy v1.0.3 / commit
`208ba4afabffab9edde416f2dbb8ae459e34adb8`; 51 copied upstream files match
byte-for-byte. GNU C11 / C++20, Release, shared Qt, strict reference-Qt mode.

The restricted workspace initially lacked Qt/CMake. Reference Ubuntu packages
were downloaded/extracted locally without installing into the host. The build
has no network acquisition step. Ordinary README commands target a regular
Ubuntu desktop. Compilation emitted upstream multi-character-constant and
unchecked stdio-result warnings; no application-source warnings remained.

### Completed checks

| Check | Evidence / result |
| --- | --- |
| Build | Native application, test executables, and ROM assets built in strict Qt 6.4.2 mode. |
| Trace parity | 90 frame outputs with identical controlled Right/Left/Up/Down input. Full serialized state, registers, WRAM window, OAM, pixels, ticks and opcode counts equal with capture on/off. |
| Pause | Repeated snapshots and a timed GUI pause do not advance state. |
| Instruction step | One subsequent opcode per demo step; PC, next storage disassembly, memory and tick interval agree. |
| Frame step | One next output callback; output boundary and CPU cursor are correctly labeled after the enclosing step. |
| Learning connection | Right reaches the symbol for `LD [$C000], A`; actual before/requested/after bytes and writer PC match. OAM X becomes `player_x + 8`; the completed framebuffer's star moves exactly one pixel. |
| Inspection purity | Every 128-byte window across 64 KiB inspected, including IO/OAM/echo/unmapped areas, without changing full serialized state. |
| Mapping / names | Echo aliases resolve. A modified ROM loses semantic names. Invalid load preserves current emulation. |
| Bounds | 300 frames with 16-record capture: history stays bounded, evictions reported. Capture toggles clear stale evidence. Position stays in bounds. |
| IRQ/HALT | Original fixture's interrupt stack writes have no stale opcode identity. HALT step reports no opcode and publishes time actually advanced. |
| Ownership | Other-thread inspection rejects access. |
| Qt controls | Rendered run/pause/step controls synchronize visible register/memory/instruction text; arrows work through table focus; focus loss releases input; memory selection follows its writer and write selection follows its byte. |
| Responsiveness | A separate 5 ms timer fired 24–26 times in QTest's 250 ms run interval. QTest polling affects the count; this demonstrates event-loop progress, not a latency bound. |
| Native X11 | Widget suite passed with the `xcb` plugin under virtual X11, in addition to offscreen. Default 1280×930 and minimum 1040×790 renders inspected. |
| Installed application | Production executable launched from an unrelated directory with no ROM argument and rendered its embedded game on X11. No browser/service needed. |
| ROM | Second assembly matches ROM/boot/header/symbol/manifest bytes; both cartridge checksums pass. Reference SHA-256: `e7ef9ff22686174b0ccfabc86e44fd3fcf65561f486cb4f236cb674a69d9d672`. |
| Vendor | All unchanged upstream inputs match their SHA-256 manifest. |
| Package | Install tree and Ubuntu `.deb` generated and inspected: dependencies, executable, source/binary ROM, assembler, desktop/icon and license notices. Installed executable has no build RPATH/RUNPATH. |

CTest: **4/4 passed**, about 1.18 seconds here. X11 widgets are an additional
run; a headless engine alone is not presented as GUI validation.

Independent GitHub CI also passed:
[Native Linux slice, run 2](https://github.com/esotericode/blink/actions/runs/37391274288),
implementation commit `a05d42798cc4c4f6cc00ffdfe314d66b85c24ea3`. The regular
Ubuntu 24.04 runner installed dependencies, built in strict Qt mode, passed all
four suites and ordinary `xvfb-run` native interactions, and generated/inspected
the install tree and `.deb`. This is separate from local measurements and does
not establish a physical desktop or actual clean-machine package installation.
The subsequent handoff update changes documentation only.

The development-only virtual-X11 helper needed path relocation because this
sandbox cannot write `/tmp` or provide `/usr/bin/xkbcomp`. Server/tests ran in
the same process tree. No such changes are needed by ordinary `xvfb-run` or
shipped in the application. A physical monitor/compositor was not tested.

### Measurements

One 300-frame traced run averaged **1,002 μs/frame**; a 3 ms live quantum
measured **3,003 μs**. DMG frame period is about 16.74 ms. This supports the
single-owner design here, not a real-time or worst-case guarantee. Qt rendering
and publication are not included in the engine average. Trace memory is bounded
by configuration; total process memory was not benchmarked.

### Reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOBSERVATORY_STRICT_DEPENDENCIES=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/engine_tests
mkdir -p out
xvfb-run -a ./build/widget_tests out/desktop.png out/minimum.png
cmake --install build --prefix "$PWD/out/install"
cpack --config build/CPackConfig.cmake -B out
dpkg-deb --info out/console-observatory_*_amd64.deb
```

See README for prerequisites and ROM rebuild instructions. The installed app
uses normal Qt runtime packages and its embedded lesson.

### Remaining validation

Physical desktop focus/compositor/DPI/accessibility; clean-machine `.deb`
installation/dependency acquisition; different Qt/compiler versions; macOS and
Windows; external mooneye/blargg ROMs; commercial compatibility; DMA/PPU hooks;
audio playback; replay fidelity. Add checks appropriate to the next feature
rather than extrapolating this controlled result into broader precision.
