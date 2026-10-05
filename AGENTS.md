# Guidance for AI contributors

Read `PROJECT_GUIDE.md` before changing the project. If present, read `README.md`, `STATUS.md`, and `DECISIONS.md` to understand the actual implementation. Existing source and verification results determine what works; this starter guide does not establish that any code exists.

## Preserve the purpose

Build a standalone native desktop application that connects a running game's behavior to CPU instructions, memory, and hardware events. Normal use must not depend on a browser, web-app wrapper, local web server, account, or separate backend. Start with monochrome Game Boy, SameBoy, C++20, Qt 6 Widgets, and CMake unless a documented technical finding supports a better starting choice. Keep the desktop requirement intact.

## Work practically

- Implement small, working vertical slices. Keep the game visible and inspectors synchronized.
- Use actual emulator state and bounded traces. Label summaries, teaching models, unsupported detail, and incomplete captures.
- Keep core changes small. Pin dependencies and document how to build the teaching ROM.
- Separate emulator ownership, snapshots/traces, UI, and annotations. Avoid speculative frameworks and premature multi-console abstractions.
- Verify behavior with tracing on and off. Test stepping and state consistency, not just screenshots.
- Make reasonable reversible decisions yourself. Ask only when missing information materially blocks progress; explain discoveries and tradeoffs.

## Leave useful continuity

Update build instructions and `STATUS.md` with what works, what was checked, limitations, and the next concrete task. Record consequential choices and evidence in `DECISIONS.md`. Distinguish an implemented feature from a sketch or proposal.

The roadmap is sequencing guidance, not a ban on new ideas. Explore improvements that advance understanding, fidelity, or usability; document material changes. Do not silently replace explicit user requirements or invent semantic game labels without evidence.
