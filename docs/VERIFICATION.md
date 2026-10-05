# Verification — 2026-10-05

These results cover this implementation and controlled teaching ROM, not all
SameBoy-supported games or desktop platforms.

## Reference build

Ubuntu 24.04 x86-64, GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, Python 3.12.3,
Qt 6.4.2 (`6.4.2+dfsg-21.1build5`). SameBoy v1.0.3 / commit
`208ba4afabffab9edde416f2dbb8ae459e34adb8`; 51 copied upstream files match
byte-for-byte. GNU C11 / C++20, Release, shared Qt, strict reference-Qt mode.

The restricted workspace initially lacked Qt/CMake. Reference Ubuntu packages
were downloaded/extracted locally without installing into the host. The build
has no network acquisition step. Ordinary README commands target a regular
Ubuntu desktop. Compilation emitted upstream multi-character-constant and
unchecked stdio-result warnings; no application-source warnings remained.

## Completed checks

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

## Measurements

One 300-frame traced run averaged **1,002 μs/frame**; a 3 ms live quantum
measured **3,003 μs**. DMG frame period is about 16.74 ms. This supports the
single-owner design here, not a real-time or worst-case guarantee. Qt rendering
and publication are not included in the engine average. Trace memory is bounded
by configuration; total process memory was not benchmarked.

## Reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOBSERVATORY_STRICT_DEPENDENCIES=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/engine_tests
mkdir -p out
xvfb-run -a ./build/widget_tests out/desktop.png out/minimum.png
cmake --install build --prefix "$PWD/out/install"
cpack --config build/CPackConfig.cmake -B out
dpkg-deb --info out/console-observatory_0.1.0_amd64.deb
```

See README for prerequisites and ROM rebuild instructions. The installed app
uses normal Qt runtime packages and its embedded lesson.

## Remaining validation

Physical desktop focus/compositor/DPI/accessibility; clean-machine `.deb`
installation/dependency acquisition; different Qt/compiler versions; macOS and
Windows; external mooneye/blargg ROMs; commercial compatibility; DMA/PPU hooks;
audio playback; replay fidelity. Add checks appropriate to the next feature
rather than extrapolating this controlled result into broader precision.
