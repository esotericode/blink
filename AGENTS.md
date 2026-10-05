# Guidance for AI contributors

Read `PROJECT_GUIDE.md` before changing the project. If present, read `README.md`, `STATUS.md`, and `DECISIONS.md` to understand the actual implementation. Existing source and verification results determine what works; this starter guide does not establish that any code exists.

The first Linux slice now exists. Also read `docs/TRACE_CONTRACT.md` and
`docs/VERIFICATION.md`. `DEPENDENCIES.lock.json` and SameBoy's `SHA256SUMS`
identify the reference inputs. Keep those files synchronized with changes.

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
- Run the four CTest suites after emulator/observation changes. Run
  `xvfb-run -a ./build/widget_tests` for native X11 UI changes; offscreen tests
  alone do not establish compositor behavior. Inspect rendered layouts.
- Vendor files are byte-for-byte upstream. Integration changes belong outside
  `third_party/sameboy`; intentional upstream upgrades require a new pin,
  manifest, callback review, parity results, and updated dependency notices.
- The next milestone is a pause-on-write movement lesson plus a sprite/tile
  inspector. Replay, commercial-ROM support, DMA, audio playback, and additional
  platforms are not already implemented.

## Leave useful continuity

Update build instructions and `STATUS.md` with what works, what was checked, limitations, and the next concrete task. Record consequential choices and evidence in `DECISIONS.md`. Distinguish an implemented feature from a sketch or proposal.

The roadmap is sequencing guidance, not a ban on new ideas. Explore improvements that advance understanding, fidelity, or usability; document material changes. Do not silently replace explicit user requirements or invent semantic game labels without evidence.
