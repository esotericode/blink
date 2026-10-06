#include "teaching/lesson.hpp"
#include "teaching_rom.hpp"

namespace observatory {
namespace {
std::string code(const std::string& text) { return "<code>" + text + "</code>"; }
std::string value(std::uint8_t v) { return hex(v, 2) + " (" + std::to_string(v) + ")"; }
std::string mark(const char* text) { return std::string("<span style='color:#ff5fa2'>") + text + "</span>"; }
std::string writer(const LessonEvidence& e) {
    if (!e.write || !e.write->instruction) return code("an interrupt or wait step");
    return code(disassemble(*e.write->instruction)) + " at " + code(hex(e.write->instruction->pc));
}
std::string effort(const LessonEvidence& e) {
    return "Getting here ran " + std::to_string(e.instructions) + " instructions and completed " +
           std::to_string(e.frames) + (e.frames == 1 ? " frame" : " frames") + ".";
}
}
int lessonStepNumber(LessonStep step) {
    switch (step) {
    case LessonStep::Holding: return 1;
    case LessonStep::Stored: return 2;
    case LessonStep::Copied: return 3;
    case LessonStep::Drawn: return 4;
    case LessonStep::Tile: return 5;
    case LessonStep::Done: return 6;
    default: return 0;
    }
}
LessonPage lessonPage(LessonStep step, const Snapshot& s, const LessonEvidence& e) {
    switch (step) {
    case LessonStep::NeedsTeachingRom:
        return {"This lesson needs the bundled teaching ROM",
                "The stops and variable names in this lesson come from the teaching ROM's own source. "
                "With another ROM the hardware views, captured writes, and <b>Run until written</b> still work, "
                "but no game variables are named.",
                "Load the teaching ROM"};
    case LessonStep::Start:
        return {"Follow one press of Right",
                "Watch a button press travel through the machine:<br><b>joypad → CPU → WRAM (" + code("player_x") +
                ") → CPU → OAM (sprite X) → PPU → LCD</b>.<br><br>Each step runs the real emulator until a specific "
                "event happens, then pauses so every panel shows the same moment. The star is at player_x = " +
                std::to_string(s.playerX) + ".",
                "1 · Hold Right"};
    case LessonStep::Holding:
        return {"Step 1 of 5 · The joypad",
                "The lesson is holding <b>Right</b>, exactly like your arrow key. Nothing has run yet.<br><br>"
                "Once per frame the game writes " + code("$20") + " to " + code("JOYP ($FF00)") +
                " to select the direction keys, then reads the low four bits back. A pressed key reads as 0, so the "
                "code inverts them with " + code("CPL") + " and stores the result in " + code("buttons") +
                ". Bit 0 is Right.",
                "2 · Run until player_x is written"};
    case LessonStep::Stored:
        return {"Step 2 of 5 · The CPU stores player_x",
                "Paused right after " + writer(e) + ". It wrote <b>" + (e.write ? value(e.write->requested) : "?") +
                "</b> to " + code("player_x") + " at " + code("$C000") + "; the byte held " +
                (e.write ? value(e.write->before) : "?") + " before. " + effort(e) + "<br><br>"
                "The memory inspector has " + code("$C000") + " selected (gold means changed) and register A still holds "
                "the stored value. The picture has <b>not</b> changed: so far only one byte of work RAM has.",
                "3 · Run until OAM X is written"};
    case LessonStep::Copied:
        return {"Step 3 of 5 · The CPU copies the position into OAM",
                writer(e) + " stored player_x + 8 = <b>" + (e.write ? value(e.write->requested) : "?") +
                "</b> into sprite 0's X byte at " + code("$FE01") + ". Object Attribute Memory holds 40 four-byte "
                "sprite records: Y+16, X+8, tile, attributes. " + effort(e) + "<br><br>The " + mark("outline") +
                " on the game shows where OAM now places the sprite, but the picture is still the earlier frame: "
                "the PPU has not drawn since this write.",
                "4 · Advance one frame"};
    case LessonStep::Drawn:
        return {"Step 4 of 5 · The PPU draws the new frame",
                "While producing output #" + std::to_string(s.frames) + ", the PPU read sprite 0's record from OAM and its "
                "tile from VRAM. The star moved one pixel to the right: <b>" + std::to_string(e.changedPixels) +
                " pixels</b> differ from output #" + std::to_string(s.previousFrame) + " (" + mark("marked") +
                ").<br><br>A frame takes 70,224 CPU clock cycles, about 16.7 ms. The CPU's write happened earlier; "
                "the screen only shows it once the PPU draws.",
                "5 · Show the tile bits"};
    case LessonStep::Tile:
        return {"Step 5 of 5 · Tile bits become pixels",
                "Sprite 0 uses tile " + std::to_string(s.video.oam[2]) + ", stored at " + code(hex(tileAddress(s.video.oam[2]))) +
                "–" + code(hex(tileAddress(s.video.oam[2]) + 15)) + " in VRAM. Each 8-pixel row is two bytes: the first "
                "(low plane) gives bit 0 of every pixel's colour number, the second (high plane) gives bit 1.<br><br>"
                + code("OBP0") + " = " + code(hex(s.video.obp0, 2)) + " maps colour numbers to shades. For sprites, colour 0 "
                "is transparent, which is why the background shows around the star.",
                "Finish and release Right"};
    case LessonStep::Done:
        return {"Lesson complete",
                "You followed one press from the joypad register, through an instruction that stored "
                + code("player_x") + ", a copy into OAM, and the PPU drawing a new frame, down to the tile bits that "
                "make the star.<br><br>Try next: select any memory byte and press <b>F9</b> to run until it is written, "
                "or open <b>Memory map</b> to see which addresses the game touches every frame.",
                "Start again"};
    }
    return {};
}
} // namespace observatory
