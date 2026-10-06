#pragma once
#include "emulator/state.hpp"
#include <optional>
#include <string>

// "Follow one press of Right": a guided path through the bundled teaching ROM.
// Each step is ended by a real emulator event; text is filled from that evidence.
namespace observatory {
enum class LessonStep { Start, Holding, Stored, Copied, Drawn, Tile, Done, NeedsTeachingRom };
inline constexpr int lessonStepCount = 5;
struct LessonEvidence {
    std::optional<WriteEvent> write;          // The write that ended the step, if any.
    std::uint64_t instructions{}, frames{};   // Work done while running to it.
    int changedPixels{};                      // Between the two latest outputs.
};
struct LessonPage {
    std::string heading, body, action; // body is Qt rich text (a small HTML subset)
};
LessonPage lessonPage(LessonStep step, const Snapshot& snapshot, const LessonEvidence& evidence);
int lessonStepNumber(LessonStep step); // 0 before the first step, 1..5, 6 when done
} // namespace observatory
