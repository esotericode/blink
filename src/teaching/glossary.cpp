#include "teaching/glossary.hpp"
#include <map>

namespace observatory {
namespace {
using Map = std::map<std::string, Explanation>;
const Map& entries() {
    static const Map map{
        // Panels (dock titles and tabs).
        {"panel.system", {"System overview",
            "A map of the Game Boy's main parts and how data moves between them. The live values update with the "
            "game; the numbers on arrows count writes in the last update.",
            "Click a part to open the panel that inspects it."}},
        {"panel.cpu", {"CPU · instruction boundary",
            "The processor's registers, its flags, and the instructions just run and about to run. Every panel shows "
            "the machine at this same moment, between two instructions.", {}}},
        {"panel.memory", {"Memory",
            "128 bytes of the Game Boy's 64 KiB address space at a time. Everything the CPU can reach (program, "
            "variables, graphics, hardware registers) has an address here.",
            "Click a byte to see which instruction last wrote it; F9 runs until it is written again."}},
        {"panel.cartridge", {"Cartridge · banks",
            "How the cartridge's memory reaches the CPU. Big games hold more than the CPU can see at once, so a chip "
            "on the cartridge swaps 16 KiB \"banks\" in and out. This panel shows which banks are visible now and every switch.",
            {}}},
        {"panel.tiles", {"Sprites and tiles",
            "The sprite table (OAM) and the 8×8 tile graphics stored in video RAM, decoded down to the bits that "
            "become each pixel.", "Click a sprite row or a tile to look inside it."}},
        {"panel.map", {"Memory map",
            "All 65,536 addresses in one picture, coloured by how often the CPU wrote to each or ran code from it. "
            "It shows where a game keeps its code and where its data changes.", {}}},
        {"panel.writes", {"Captured writes",
            "The most recent times the CPU wrote to memory, newest first, each with the instruction that did it.",
            "Click a row to select that byte in the Memory panel."}},
        {"panel.information", {"Selection info",
            "Detailed offline explanations for the item you selected most recently. Read how it works, how games use it, and what the current observation can tell you.",
            "Select an item, browse a topic, or follow related links. Reading never runs the game."}},
        {"info.back", {"Back", "Return to the previous topic or selection. This navigates explanations, not emulated time.", {}}},
        {"info.forward", {"Forward", "Revisit the next explanation in your reading history.", {}}},
        {"info.topics", {"Browse a hardware topic", "Read the full hardware reference without needing to select an item first. Related links connect concepts such as memory, banks, tiles, and sprites.", {}}},
        {"tiles.maps", {"Background and window maps", "A reconstructed 32×32 grid of tile references from current VRAM, using the current addressing mode and BGP. Select the background, window, or either physical map.", "Click a cell for its entry, resolved pattern, and detailed map explanation."}},

        // Toolbar and menu actions.
        {"action.run", {"Run / pause · F5",
            "Let the game run in real time, or freeze it. While paused, nothing changes until you step, so you can "
            "inspect everything at leisure.", {}}},
        {"action.step", {"Step one instruction · F10",
            "Run exactly one more CPU instruction, then pause. The smallest step: registers and memory change one "
            "instruction at a time.",
            "If the CPU is halted waiting for an interrupt, time passes until it wakes and runs one."}},
        {"action.frame", {"Step one frame · F11",
            "Run until the screen finishes its next picture, about 1/60 of a second or several thousand instructions, "
            "then pause.", {}}},
        {"action.until", {"Run until written · F9",
            "Run until something writes the selected memory byte, then pause right after it. The quickest way to "
            "find the code responsible for a value.",
            "Needs Capture writes. Gives up after one emulated second."}},
        {"action.restart", {"Restart · Ctrl+R",
            "Switch the console off and on again with the same cartridge. Battery-backed save memory is kept, as on "
            "a real cartridge.", {}}},
        {"action.capture", {"Capture writes",
            "Record CPU write attempts (the newest 4,096 by default) with the instruction when known, and observed OAM DMA requests. "
            "\"Last writer\" answers and Run until written depend on it.",
            "Switching it off or on starts a fresh record, so old answers cannot go stale."}},
        {"action.open", {"Open ROM · Ctrl+O",
            "Load a Game Boy cartridge image (.gb or .gbc file) up to 8 MiB. It starts paused at power-on.",
            "You can also drop a ROM file onto the window."}},
        {"action.teaching", {"Teaching game",
            "A tiny original game made for this app: a star you move with the arrow keys. Source-defined variables help connect code, memory, and graphics.", {}}},
        {"action.bankdemo", {"Bank-switching demo",
            "An original cartridge with four ROM banks. Each press of A swaps a different bank in at the same "
            "address, and the screen shows which bank's code ran.", {}}},
        {"action.savebattery", {"Save battery RAM · Ctrl+S",
            "Write the cartridge's save memory to the .sav file next to the ROM now.",
            "It is also saved automatically every few seconds while it changes, and on exit."}},
        {"action.savebatteryas", {"Save battery RAM elsewhere",
            "Choose another .sav destination for the current game. Use this if its folder is unwritable or an existing save could not be loaded. Saving successfully keeps future autosaves at the new location.", {}}},
        {"cart.save", {"Battery save status",
            "Shows where this game saves and whether progress is unsaved. If saving fails, you are asked before closing or switching games. An unreadable existing save is never overwritten automatically; save elsewhere to keep new progress.", {}}},
        {"action.sprites", {"Sprite outlines",
            "Draw a box where the sprite table (OAM) places each sprite right now. After the CPU moves a sprite, the "
            "box moves first; the picture catches up when the next frame is drawn.", {}}},
        {"action.changes", {"Changed pixels",
            "Highlight pixels that differ between the last two completed pictures, to see exactly what one frame changed.",
            {}}},
        {"action.clearmap", {"Clear memory map",
            "Reset the memory map's counts and start a new counting interval from now.", {}}},
        {"action.resetlayout", {"Reset layout",
            "Put every panel back in its default place and size.", {}}},

        // Status and picture.
        {"ui.badge", {"Paused or running",
            "While paused, nothing changes until you step, so the panels describe one exact moment; the picture and "
            "recorded writes are labelled with their own times. While running, panels refresh about 30 times a second.",
            {}}},
        {"ui.cursor", {"The shared moment",
            "t is emulated time since power-on, in ticks of 1/8,388,608 s (two per CPU clock cycle). Instruction # "
            "counts instructions run; output # counts finished screen pictures.",
            "The CPU, memory and other state panels all show this point; the picture and recorded writes carry their own times."}},
        {"ui.game", {"The Game Boy screen",
            "The most recently finished 160×144 picture. When you step instruction by instruction it only changes "
            "once the console finishes drawing a new frame.",
            "Keys: arrows = D-pad · Z = A · X = B · Enter = Start · Backspace = Select."}},
        {"ui.frame", {"Which picture is shown",
            "Pictures arrive once per frame, so the one on screen can be older than the CPU's current moment. "
            "\"VBlank frame\" is a normal finished picture.", {}}},
        {"ui.activity", {"Last update",
            "What happened in the last update: how many instructions ran, and how many times the CPU tried to write to each "
            "kind of memory. Clicking around while paused keeps showing that last update.", {}}},

        // CPU registers and flags.
        {"reg.af", {"AF · accumulator and flags",
            "A is the CPU's main working register: arithmetic, comparisons and most data moves go through it. "
            "F holds the four flags (Z, N, H, C) in its top bits.", {}}},
        {"reg.bc", {"BC · register pair",
            "Two 8-bit registers, B and C, that can also act as one 16-bit value. Programs often use them as a "
            "counter or a second address.", {}}},
        {"reg.de", {"DE · register pair",
            "Two 8-bit registers, D and E, usable as one 16-bit value. Often holds the source address when copying data.",
            {}}},
        {"reg.hl", {"HL · the pointer pair",
            "H and L together usually hold a memory address. Many instructions read or write \"[HL]\", the byte HL "
            "points at, which makes HL the CPU's favourite way to walk through tables.", {}}},
        {"reg.sp", {"SP · stack pointer",
            "Points at the top of the stack, a last-in-first-out pile of 16-bit values in RAM. CALL pushes a return "
            "address there and RET takes it back.", {}}},
        {"reg.pc", {"PC · program counter",
            "The address of the next instruction. It moves past each instruction as it runs; jumps, calls, returns "
            "and interrupts set it somewhere new.", {}}},
        {"flag.z", {"Z · zero flag",
            "Set (1) when the last calculation gave zero. JR Z and JR NZ jump depending on it, which is how loops "
            "and if-statements work.", {}}},
        {"flag.n", {"N · subtract flag",
            "Set after a subtraction, cleared after an addition. Only DAA, the decimal-adjust instruction, uses it.", {}}},
        {"flag.h", {"H · half-carry flag",
            "Set when a result carried out of the low four bits. Used for decimal (BCD) arithmetic, such as score counters.",
            {}}},
        {"flag.c", {"C · carry flag",
            "Depends on the instruction: an addition carry, a subtraction borrow, or the bit shifted out by a rotate or shift. After CP n, C = 1 means A was smaller than n.", {}}},
        {"cpu.instruction", {"Next and last instruction",
            "Next: the instruction stored at PC, which runs on the next step. Last: the one that just ran. On banked "
            "cartridges the bank is shown, because the same address can hold different code.",
            "Instructions are decoded from memory storage, so looking never disturbs the machine."}},

        // System overview parts and paths.
        {"part.joypad", {"Joypad",
            "The eight buttons. The CPU writes to the JOYP register ($FF00) to choose the D-pad or the A/B/Select/Start "
            "group, then reads which are pressed (0 means pressed).", "Click to show $FF00 in the Memory panel."}},
        {"part.cartridge", {"Cartridge",
            "The game itself: ROM chips holding the program and graphics, and on bigger games a memory bank "
            "controller (MBC) and battery-backed save RAM.", "Click to open the Cartridge panel."}},
        {"part.cpu", {"CPU · Sharp SM83",
            "The processor, a cousin of the Z80. It fetches instructions from memory one after another and carries "
            "them out, several hundred thousand a second (its clock runs at 4.19 MHz).", "Click to open the CPU panel."}},
        {"part.wram", {"WRAM · work RAM",
            "8 KiB of general-purpose memory at $C000. Games keep their variables here: positions, scores, timers.",
            "Click to show $C000 in the Memory panel."}},
        {"part.oam", {"OAM · sprite table",
            "Object Attribute Memory: 40 sprites × 4 bytes (Y, X, tile, flags) at $FE00. The PPU reads it to place "
            "moving objects on screen.", "Click to open Sprites and tiles."}},
        {"part.vram", {"VRAM · video RAM",
            "8 KiB at $8000 holding the 8×8 tile graphics and the background maps that say which tile goes where.",
            "Click to open Sprites and tiles."}},
        {"part.ppu", {"PPU · picture processing unit",
            "Draws the screen one line at a time, 154 lines per frame (144 visible), reading tiles from VRAM and "
            "sprites from OAM. Its registers at $FF40–$FF4B set scrolling, palettes and layers.",
            "Click to show its registers in the Memory panel."}},
        {"part.lcd", {"LCD",
            "The 160×144 screen with four shades. A new picture arrives about 60 times a second (59.7 Hz).", {}}},
        {"path.write", {"CPU writes",
            "The CPU storing values into this memory. The number counts write attempts in the last update.", {}}},
        {"path.read", {"PPU reads",
            "While drawing, the PPU reads sprite entries from OAM and tile graphics from VRAM on its own; the CPU "
            "isn't involved.", {}}},
        {"path.lcd", {"Pixels to the screen", "Each finished line of pixels goes to the LCD.", {}}},
        {"path.joypad", {"Button state", "The CPU reads the buttons through the JOYP register.", {}}},
        {"path.rom", {"Program and data",
            "The CPU reads its instructions and data from the cartridge ROM.", {}}},
        {"path.dma", {"OAM DMA",
            "A hardware copy of 160 bytes into OAM, usually from a \"shadow\" sprite table in work RAM. The CPU "
            "starts it by writing a page number to $FF46 and waits while the copy runs.",
            "Most commercial games update their sprites this way once per frame."}},

        // Memory panel.
        {"memory.region", {"Jump to a region",
            "Each part of the address space has a job: program, graphics, variables, sprite table, hardware registers. "
            "Pick one to look at it.", {}}},
        {"memory.address", {"Go to an address",
            "Type a hexadecimal address from 0000 to FFFF and press Enter.",
            "Numbers starting with $ are hexadecimal (base 16): $10 = 16, $FF = 255."}},
        {"memory.selection", {"Selected byte",
            "The byte you picked: its address, any known name, the memory region, and its value in hexadecimal and decimal.",
            {}}},
        {"memory.window", {"Which bank you are looking at",
            "Cartridge addresses show whichever bank is mapped there now, so the same address can show different bytes later.",
            {}}},

        // Cartridge panel.
        {"cart.facts", {"Cartridge header",
            "Facts the cartridge declares about itself at $0100–$014F: its title, controller type, ROM and RAM size, "
            "and a checksum of the header.", {}}},
        {"cart.window.rom0", {"$0000–$3FFF · lower ROM window",
            "The first 16 KiB of cartridge ROM the CPU can see. It is almost always bank 0, which holds the startup "
            "code and common routines. Some controllers can remap it in particular modes.", "Click to view it in the Memory panel."}},
        {"cart.window.romx", {"$4000–$7FFF · switchable ROM window",
            "The second 16 KiB window. On cartridges with a bank controller (MBC), the game chooses which 16 KiB bank "
            "appears here by writing a bank number to a ROM address.", "Click to view it in the Memory panel."}},
        {"cart.window.ram", {"$A000–$BFFF · cartridge RAM window",
            "8 KiB of extra memory on the cartridge, often kept alive by a battery to hold save games. The game "
            "must switch it on before using it; some cartridges have none.", "Click to view it in the Memory panel."}},
        {"cart.bank", {"ROM bank",
            "One 16 KiB slice of the cartridge. Brighter blue means more instructions ran from it.", {}}},
        {"cart.rambank", {"RAM bank", "One 8 KiB slice of cartridge RAM.", {}}},
        {"cart.run", {"Run until the bank changes",
            "Run until the cartridge maps a different bank, then pause right after the instruction that switched it.",
            "Gives up after one emulated second; many games switch only when something new happens."}},
        {"cart.switches", {"Bank controller writes",
            "Each time the CPU wrote to a ROM address. ROM can't change, so the MBC chip treats these writes as "
            "commands: switch bank, enable RAM, and so on.", {}}},
        {"cart.stats", {"Bank activity",
            "How often the mapping changed and how many instructions ran from each bank since the memory map was last cleared.",
            {}}},

        // Sprites and tiles.
        {"tiles.oam", {"OAM · the sprite table",
            "40 sprites, 4 bytes each, at $FE00. Grey rows are placed off-screen (games hide unused sprites that way).",
            "Click a row to see its tile."}},
        {"tiles.col.index", {"Sprite number",
            "0–39. Where sprites overlap, the one further left is drawn on top (at equal X, the lower number). At most "
            "10 sprites can appear on one line.", {}}},
        {"tiles.col.y", {"Y + 16",
            "Vertical position plus 16. Y + 16 = 16 puts the sprite's top on the first screen line; 0 hides it above the screen.",
            {}}},
        {"tiles.col.x", {"X + 8",
            "Horizontal position plus 8. X + 8 = 8 puts the sprite at the left edge; 0 hides it.", {}}},
        {"tiles.col.tile", {"Tile number", "Which 8×8 tile (counted from $8000) the sprite shows.", {}}},
        {"tiles.col.attr", {"Attributes",
            "Flag bits: 7 = behind the background, 6 = flip vertically, 5 = flip horizontally, 4 = use palette OBP1 "
            "instead of OBP0.", {}}},
        {"tiles.palette", {"Colours",
            "Each pixel stores a colour number 0–3; a palette register turns it into one of four shades. Pick which "
            "palette to view the tiles through, or show the raw numbers.", {}}},
        {"tiles.block", {"Tile block",
            "Tile graphics live in three 2 KiB blocks of 128 tiles. Sprites use $8000–$8FFF; the background uses "
            "$8000 or $9000 depending on LCDC bit 4.", {}}},
        {"tiles.sheet", {"Tile sheet",
            "Every tile in the block, decoded from video RAM. Teal outlines mark tiles that on-screen sprites use.", {}}},
        {"tiles.low", {"Bit plane 0 (low bits)",
            "The first byte of each tile row. Each of its 8 bits is the low bit of one pixel's colour number.", {}}},
        {"tiles.high", {"Bit plane 1 (high bits)",
            "The second byte of each tile row. Each bit is the high bit of one pixel's colour number.", {}}},
        {"tiles.pixels", {"Colour numbers",
            "Each pixel's two bits combined: high bit × 2 + low bit gives 0–3. The palette then picks the shade.", {}}},
        {"tiles.source", {"Where OAM's contents came from",
            "OAM changes when the CPU stores to it, or when the game asks the hardware to copy 160 bytes in one go (OAM "
            "DMA), usually from a table in work RAM. The app records each request and checks OAM afterwards; it can't "
            "watch individual bytes move.", "Click the source address to see what the app recorded writing it."}},

        // Memory map.
        {"map.view", {"Memory map",
            "One row per 256 addresses; orange shows CPU writes, blue shows instructions run. Bright means busy.",
            "Click an address to inspect it."}},
        {"map.mode", {"What to colour", "Show write attempts, instructions run, or both.", {}}},
        {"map.clear", {"Clear", "Start a new counting interval from now.", {}}},

        // Captured writes.
        {"writes.tick", {"When",
            "The end of the emulator step that contained the write, in ticks since power-on.", {}}},
        {"writes.instruction", {"Instruction", "The CPU instruction that wrote, with its address.", {}}},
        {"writes.address", {"Where", "The address written and its name when known.", {}}},
        {"writes.status", {"What the record holds",
            "How many writes are kept (the newest 4,096 by default). Older ones are dropped and counted, so you always "
            "know when history is incomplete. Selecting a write shows evidence about the past; it doesn't rewind the machine.",
            {}}},
        {"writes.values", {"Before → after",
            "The stored byte before and after the write. For cartridge controller writes, the effect on the banks instead.",
            {}}},

    };
    return map;
}
}

const Explanation& glossary(const std::string& key) {
    static const Explanation none;
    const auto& map = entries();
    const auto found = map.find(key);
    return found == map.end() ? none : found->second;
}
std::size_t glossarySize() { return entries().size(); }
bool glossaryHas(const std::string& key) { return entries().count(key) != 0; }

Explanation regionExplanation(std::uint16_t a) {
    if (a < 0x4000) return {"Cartridge ROM · lower window",
        "Read-only program and data from the cartridge, usually bank 0. Some controllers can remap this 16 KiB window in particular modes; the inspector shows the current bank.", {}};
    if (a < 0x8000) return {"Cartridge ROM · switchable window",
        "Another 16 KiB of the cartridge. If the cartridge has a bank controller (MBC), the game chooses which bank "
        "appears here, so the same address can hold different code at different times.", {}};
    if (a < 0x9800) return {"VRAM · tile graphics",
        "Video RAM holding 8×8-pixel tiles, 16 bytes each (2 bits per pixel). The PPU builds the background and "
        "sprites from them.", {}};
    if (a < 0xA000) return {"VRAM · tile maps",
        "Two 32×32 grids of tile numbers. Each byte picks the tile for one 8×8 square of the background or window layer.",
        {}};
    if (a < 0xC000) return {"Cartridge RAM",
        "Extra memory on the cartridge, often battery-backed to keep save games. The game must enable it first; "
        "some cartridges have none.", {}};
    if (a < 0xE000) return {"WRAM · work RAM",
        "The console's 8 KiB of general-purpose memory. Games keep variables here (positions, scores, timers) and "
        "often a \"shadow\" copy of the sprite table.", {}};
    if (a < 0xFE00) return {"Echo RAM",
        "A mirror of work RAM: reading or writing here reaches the same bytes 8 KiB lower. Games rarely use it.", {}};
    if (a < 0xFEA0) return {"OAM · sprite table",
        "Object Attribute Memory: 40 sprites × 4 bytes (Y, X, tile, flags). The PPU reads it every line to place "
        "moving objects. Games fill it with CPU stores or with an OAM DMA copy.", {}};
    if (a < 0xFF00) return {"Unusable",
        "Nothing is connected here on the original Game Boy.", {}};
    if (a < 0xFF80) return {"Hardware registers (I/O)",
        "Not ordinary memory: each address is a control or status port of part of the console, such as buttons, "
        "timer, sound or screen. The value shown is the stored register, which can differ from what the CPU would read.",
        {}};
    if (a < 0xFFFF) return {"HRAM · high RAM",
        "127 bytes of memory inside the main chip. The CPU can still reach it during an OAM DMA copy, so games "
        "keep their DMA routine here.", {}};
    return {"IE · interrupt enable",
        "Each bit lets one kind of interrupt (VBlank, LCD status, timer, serial, joypad) pause the program and run "
        "its handler.", {}};
}

Explanation ioRegisterExplanation(std::uint16_t a) {
    switch (a) {
    case 0xFF00: return {"JOYP · joypad",
        "Write bit 4 or 5 low to choose the D-pad or the A/B/Select/Start group, then read the low four bits: 0 means pressed.", {}};
    case 0xFF01: return {"SB · serial data", "The byte to send or just received over the link cable.", {}};
    case 0xFF02: return {"SC · serial control", "Starts a link-cable transfer and chooses which side drives the clock.", {}};
    case 0xFF04: return {"DIV · divider", "Counts up 16,384 times a second. Writing any value resets it to 0; games use it as a cheap random source.", {}};
    case 0xFF05: return {"TIMA · timer counter", "Counts at the speed set in TAC. When it overflows past 255 it reloads from TMA and requests a timer interrupt.", {}};
    case 0xFF06: return {"TMA · timer reload", "The value TIMA restarts from after it overflows.", {}};
    case 0xFF07: return {"TAC · timer control", "Turns the timer on and picks its speed: 4,096, 16,384, 65,536 or 262,144 counts per second.", {}};
    case 0xFF0F: return {"IF · interrupt requests",
        "One bit per interrupt source (VBlank, LCD status, timer, serial, joypad). Hardware sets a bit to ask for "
        "attention. Service needs the matching IE bit and the CPU's master interrupt enable (IME); a request can stay pending.", {}};
    case 0xFF40: return {"LCDC · display control",
        "Switches the screen, background, window and sprites on or off, and picks sprite size and which tile "
        "data and maps to use.", {}};
    case 0xFF41: return {"STAT · display status",
        "Which drawing phase the PPU is in (HBlank, VBlank, OAM search, drawing), and which of those may raise an interrupt.", {}};
    case 0xFF42: return {"SCY · scroll Y", "How far the background is scrolled down, in pixels.", {}};
    case 0xFF43: return {"SCX · scroll X", "How far the background is scrolled right, in pixels.", {}};
    case 0xFF44: return {"LY · current line",
        "The screen line being drawn: 0–143 are visible, 144–153 are the VBlank pause when games safely update graphics.", {}};
    case 0xFF45: return {"LYC · line compare", "When LY equals this value, STAT can request an interrupt; used for mid-screen effects.", {}};
    case 0xFF46: return {"DMA · OAM DMA start",
        "Writing $XX here makes the hardware copy $XX00–$XX9F into the sprite table (OAM) in 160 machine cycles.", {}};
    case 0xFF47: return {"BGP · background palette", "Maps the background's colour numbers 0–3 to shades: two bits per colour number.", {}};
    case 0xFF48: return {"OBP0 · sprite palette 0", "Maps sprite colour numbers 1–3 to shades; colour 0 is transparent.", {}};
    case 0xFF49: return {"OBP1 · sprite palette 1", "A second sprite palette, chosen by attribute bit 4.", {}};
    case 0xFF4A: return {"WY · window Y", "Screen line where the window layer (a second, non-scrolling background) starts.", {}};
    case 0xFF4B: return {"WX · window X", "Window's left edge plus 7.", {}};
    case 0xFF50: return {"Boot program switch", "Writing 1 here hides the boot program and reveals the cartridge's first 256 bytes. It can't be undone.", {}};
    default: break;
    }
    if (a >= 0xFF10 && a <= 0xFF26) return {"Sound register",
        "Controls one of the four sound channels (two square waves, a wave channel and noise) or the master volume and mixing.", {}};
    if (a >= 0xFF30 && a <= 0xFF3F) return {"Wave pattern RAM", "32 four-bit samples that sound channel 3 plays in a loop.", {}};
    return {};
}
} // namespace observatory
