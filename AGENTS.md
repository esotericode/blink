# Guidance for AI contributors

Read `PROJECT_GUIDE.md` before changing the project. If present, read `README.md`, `STATUS.md`, and `DECISIONS.md` to understand the actual implementation. Existing source and verification results determine what works; this starter guide does not establish that any code exists.

A Linux and Windows build with a guided lesson exists. Also read
`docs/TRACE_CONTRACT.md` and `docs/VERIFICATION.md`. `DEPENDENCIES.lock.json`
and SameBoy's `SHA256SUMS` identify the reference inputs. Keep those files
synchronized with changes.

## Preserve the purpose

Build a standalone native desktop application that connects a running game's behavior to CPU instructions, memory, and hardware events. Normal use must not depend on a browser, web-app wrapper, local web server, account, or separate backend. Start with monochrome Game Boy, SameBoy, C++20, Qt 6 Widgets, and CMake unless a documented technical finding supports a better starting choice. Keep the desktop requirement intact.

## Work practically

- Implement small, working vertical slices. Keep the game visible and inspectors synchronized.
- Use actual emulator state and bounded traces. Label summaries, teaching models, unsupported detail, and incomplete captures.
- Keep core changes small. Pin dependencies and document how to build the teaching ROM.
- Separate emulator ownership, snapshots/traces, UI, and annotations. Avoid speculative frameworks and premature multi-console abstractions.
- Verify behavior with tracing on and off. Test stepping and state consistency, not just screenshots.
- Make reasonable reversible decisions yourself. Ask only when missing information materially blocks progress; explain discoveries and tradeoffs.

## Implementation-specific advice

- SameBoy's general "safe read" can synchronize PPU/APU state. Do not use it
  for inspector windows, callback values, or disassembly. The adapter reads
  public direct storage; its only literal safe-read exception is FF50's mapping
  flag. Keep the all-address-space full-state purity check passing.
- An execution callback counts one opcode, but `GB_run` may instead service an
  interrupt or wait. Clear opcode identity before each atomic call, and preserve
  the interrupt/HALT checks. Do not label an enclosing interval as a write cycle.
- The write callback is pre-acceptance. Never suppress a write, and never infer
  DMA/PPU coverage from this CPU hook. Raw IO storage differs from bus readback.
- State publication and native execution have one owner on Qt's main thread.
  Keep live quanta bounded and copied snapshots consistent. Avoid a worker/plugin
  framework unless measurements expose a real requirement.
- Teaching semantics require exact ROM-byte identity, not a matching filename.
  Editing assembly regenerates binary/header/symbols via the same Python tool.
  Any user ROM loads, but it gets hardware facts and real events only, never
  invented variable names.
- Banks: read the mapping and banked storage only through
  `GB_get_direct_access` (it reports the bank). Writes below `$8000` are MBC
  commands; show their effect from `banksBefore`/`banksAfter`, not ROM bytes.
  The RAM-enable latch is not public: label cartridge RAM as storage.
- Keep `Snapshot` small on the stack (frames are heap vectors): MinGW's main
  thread has 2 MiB, and a 190 KB snapshot overflowed it under Windows.
- OAM DMA is observed (request from the `$FF46` write, OAM checked against the
  source after 162 M-cycles plus halted time), never claimed byte by byte.
- Tooltips: put explanations in `src/teaching/glossary.cpp` and reference them
  with `tips::key("…")`; custom-painted widgets call `tips::show()` with the
  hovered region. `tests/glossary_tests.py` fails on an unknown key or an
  explanation over 380 characters. Write for a smart newcomer; no unexplained
  jargon, and no claims the emulator state does not support.
- Run the CTest suites after emulator/observation changes. Run
  `xvfb-run -a ./build/widget_tests` for native X11 UI changes; offscreen tests
  alone do not establish compositor behavior. Inspect rendered layouts:
  `widget_tests desktop.png minimum.png <dir>` also saves every lesson stage
  and tab. Check the 980×680 minimum, not only the default size.
- Windows uses MinGW-w64 (Qt's kit); never require MSVC for the core. Keep
  `.gitattributes` (LF) or vendored hashes break on Windows checkouts. Every
  custom widget looked up with `findChild<T>()` needs `Q_OBJECT` (Qt ≥ 6.5).
  Windows CI must stay green, including the packaged launch with only
  System32 on PATH. Use `--screenshot` for headless launch checks.
- Lesson steps must end on real engine events (`runUntilWrite`, frame step),
  and lesson text in `src/teaching/lesson.*` may only claim what the widget
  test asserts. Keep run-until-write's parity test with untraced stepping.
- Vendor files are byte-for-byte upstream. Integration changes belong outside
  `third_party/sameboy`; intentional upstream upgrades require a new pin,
  manifest, callback review, parity results, and updated dependency notices.
- Implemented: the pause-on-write movement lesson, sprite/tile bit-plane
  inspector, memory activity map, system overview, loading any
  SameBoy-supported cartridge with battery `.sav` files, the Cartridge/bank
  panel, the bank-switching demo and lesson, observed OAM DMA records, and
  explanatory tooltips. Next: an interrupt and timer lesson and a
  background-map view. Replay, save states, CGB mode, per-byte DMA tracing,
  audio playback, and macOS are not implemented; commercial-game
  compatibility is SameBoy's and not surveyed here.

## Leave useful continuity

Update build instructions and `STATUS.md` with what works, what was checked, limitations, and the next concrete task. Record consequential choices and evidence in `DECISIONS.md`. Distinguish an implemented feature from a sketch or proposal.

The roadmap is sequencing guidance, not a ban on new ideas. Explore improvements that advance understanding, fidelity, or usability; document material changes. Do not silently replace explicit user requirements or invent semantic game labels without evidence.
