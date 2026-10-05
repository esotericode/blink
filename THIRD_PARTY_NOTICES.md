# Attribution and dependency notices

Application code, original teaching ROM, minimal boot, build tools, and icon:
MIT, Console Observatory contributors. See `LICENSE`. No commercial game,
manufacturer boot ROM, or Nintendo logo is included.

## SameBoy

SameBoy 1.0.3, Lior Halphon and contributors, upstream
https://github.com/LIJI32/SameBoy, commit
`208ba4afabffab9edde416f2dbb8ae459e34adb8`.

Vendored `Core/`, `LICENSE`, and `version.mk` are unchanged. License: Expat/MIT;
full notice at `third_party/sameboy/LICENSE` in source, or
`share/console-observatory/licenses/sameboy/LICENSE` in the installation.
Excluded iOS and HexFiend files are not used. `SHA256SUMS` records the exact
vendored inputs; `UPSTREAM.md` documents acquisition.

The core is statically linked. Its GNU C11 build disables timekeeping, rewind,
the interactive debugger, cheats, and cheat search; flags are identical for
core and header consumers. Timing belongs to the application. Audio hardware
remains emulated but there is no audio-output device integration.

## Qt

Tested Qt 6.4.2, Ubuntu package `6.4.2+dfsg-21.1build5`, unmodified, dynamic
linking. Runtime modules used directly: Core, Gui, Widgets. Their platform
plugins and transitive system dependencies are supplied by the OS package
manager. Test is used only by `widget_tests`. No Qt WebEngine or web UI is used.

Qt's open-source option for these modules is LGPLv3 (with alternatives per
upstream). License texts are included as `licenses/LGPL-3.0.txt` and
`licenses/GPL-3.0.txt`. Copyright: The Qt Company Ltd. and contributors.
Primary references:
- https://doc.qt.io/qt-6.4/licensing.html
- https://code.qt.io/cgit/qt/qtbase.git/tree/LICENSES?h=v6.4.2
- https://download.qt.io/archive/qt/6.4/6.4.2/submodules/qtbase-everywhere-src-6.4.2.tar.xz
- https://packages.ubuntu.com/noble/qt6-base-dev (source package `qt6-base`)

The generated `.deb` distributes application code, source ROMs, and notices;
it depends on OS Qt packages rather than bundling Qt libraries. Those packages
retain Qt and embedded third-party notices and provide corresponding source.
Users can replace compatible Qt shared libraries. Application source and build
instructions are available in this repository. A future bundled runtime must
include matching Qt source/patches and all applicable third-party notices; do
not treat this source build as a completed all-platform redistribution audit.

## Build-only tools

CMake 3.22+ (tested 3.28.3; BSD-3-Clause), Python 3.8+ (tested 3.12.3; PSF),
GCC 13.3 (GPL with runtime exceptions), Ninja 1.11.1 (Apache-2.0), and Qt Test.
These tools are not required to run the installed application. The subset
assembler is original Python and uses only the standard library. RGBDS and
upstream `cppp` are not required. Tool versions and verification are documented
in `docs/VERIFICATION.md`.
