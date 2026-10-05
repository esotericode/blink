# SameBoy source pin

Repository: https://github.com/LIJI32/SameBoy
Release: v1.0.3
Commit: 208ba4afabffab9edde416f2dbb8ae459e34adb8

`Core/`, `LICENSE`, `version.mk` are copied byte-for-byte. No emulator patches.
Build integration lives in the application's root CMakeLists.txt. See
THIRD_PARTY_NOTICES.md for build flags and licensing. Upgrades must re-check
callback coverage, timing, ABI flags, and trace-enabled/disabled parity.
