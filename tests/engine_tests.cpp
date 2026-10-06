#include "emulator/cartridge.hpp"
#include "emulator/engine.hpp"
#include "emulator/graphics.hpp"
#include "teaching/annotations.hpp"
#include "teaching_rom.hpp"
#include "fixtures.hpp"
#include <ctime>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <thread>

using namespace observatory;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void ready(Engine& e) {
    int visibleFrames = 0;
    for (int i = 0; i < 16; ++i) {
        e.stepFrame();
        auto state = e.snapshot();
        if (state.frameKind == "VBlank frame" && state.playerX == 72 && ++visibleFrames == 2) return;
    }
    throw std::runtime_error("Teaching ROM did not initialize");
}
int spriteLeft(const Snapshot& s) {
    auto darkness = [](std::uint32_t p) { return ((p>>16)&255) + ((p>>8)&255) + (p&255); };
    auto darkest = *std::min_element(s.pixels.begin(),s.pixels.end(),[&](auto a,auto b) { return darkness(a)<darkness(b); });
    int left = 160;
    for (int y = 0; y < 144; ++y) for (int x = 0; x < 160; ++x) {
        if (s.pixels[y*160+x] == darkest) left = std::min(left,x);
    }
    return left;
}
void parity() {
    Engine traced(128), plain(128); plain.setTraceEnabled(false);
    require(traced.stateBytes() == plain.stateBytes(),"Reset states differ");
    for (int frame = 0; frame < 90; ++frame) {
        for (auto button : {Button::Right,Button::Left,Button::Up,Button::Down}) {
            bool down = (button == Button::Right && frame >= 8 && frame < 25) ||
                        (button == Button::Left && frame >= 30 && frame < 38) ||
                        (button == Button::Up && frame >= 45 && frame < 52) ||
                        (button == Button::Down && frame >= 60 && frame < 67);
            traced.setButton(button,down); plain.setButton(button,down);
        }
        require(traced.stepFrame().completedFrame && plain.stepFrame().completedFrame,"Frame did not complete");
        require(traced.stateBytes() == plain.stateBytes(),"Tracing changed full SameBoy save-state bytes");
        auto a = traced.snapshot(), b = plain.snapshot();
        require(a.registers == b.registers && a.memory == b.memory && a.video.oam == b.video.oam && a.video.vram == b.video.vram && a.previousPixels == b.previousPixels && a.pixels == b.pixels && a.ticks == b.ticks && a.instructions == b.instructions,"Trace parity snapshot mismatch");
        require(traced.traceSize() <= traced.traceCapacity(),"Trace exceeded capacity");
    }
    auto a = traced.snapshot();
    require(a.evictedWrites > 0,"Eviction was not reported");
    require(plain.traceSize() == 0,"Disabled tracing captured writes");
    std::cout << "PASS trace on/off: 90 frames, controlled 4-direction input, full state/register/memory/OAM/pixels/ticks parity\n";
}
void steppingAndMovement() {
    Engine e; ready(e);
    auto before = e.snapshot();
    require(before.playerX == 72 && before.video.oam[1] == 80,"Initial position/OAM wrong");
    const auto initialImage = before.pixels;
    const auto oldLeft = spriteLeft(before);
    e.setButton(Button::Right,true);
    bool reached = false;
    for (int n = 0; n < 40000; ++n) {
        auto s = e.snapshot();
        if (s.next.pc == demo::write_player_x_right) { before = s; reached = true; break; }
        auto step = e.stepInstruction();
        auto after = e.snapshot();
        require(step.executedInstruction && after.instructions == s.instructions + 1,"Instruction step executed other than one opcode");
        require(after.next.pc == after.registers.pc && after.ticks == s.ticks + step.advancedTicks,"Instruction view/cursor not synchronized");
    }
    require(reached,"Right write instruction not reached");
    const auto step = e.stepInstruction(); auto after = e.snapshot();
    require(step.executedInstruction && after.instructions == before.instructions+1,"Write step wrong count");
    require(after.playerX == before.playerX+1 && after.memory[0] == after.playerX,"Named variable/memory not synchronized");
    require(after.registers.pc == demo::write_player_x_right+3 && after.next.pc == after.registers.pc,"PC not at write's successor");
    const auto& w = after.writes.back();
    require(w.address == demo::player_x && w.instruction && w.instruction->pc == demo::write_player_x_right,"Write attributed to wrong instruction");
    require(w.before == before.playerX && w.requested == after.playerX && w.after == after.playerX && w.physicalStorage,"Write values not real storage");
    require(w.startTicks == before.ticks && w.endTicks == after.ticks,"Write timestamp interval wrong");
    require(after.pixels == before.pixels,"Instruction step invented a new completed frame");
    e.setButton(Button::Right,false);
    for (int n = 0; n < 50 && e.snapshot().next.pc != demo::write_oam_x; ++n) e.stepInstruction();
    require(e.snapshot().next.pc == demo::write_oam_x,"OAM copy not reached");
    e.stepInstruction(); after = e.snapshot();
    require(after.video.oam[1] == after.playerX+8,"OAM X not synchronized with position");
    require(after.writes.back().instruction->pc == demo::write_oam_x,"OAM writer identity wrong");
    auto oldFrames = after.frames; auto frameStep = e.stepFrame(); auto output = e.snapshot();
    require(frameStep.completedFrame && output.frames == oldFrames+1,"Frame step advanced other than one output");
    require(output.frameBoundaryTicks == output.ticks && output.next.pc == output.registers.pc,"Frame-step cursor mismatch");
    if (output.pixels == initialImage || spriteLeft(output) != oldLeft+1) {
        std::cerr << "Sprite diagnostic: left " << oldLeft << " → " << spriteLeft(output)
                  << ", output=" << output.frames << " kind=" << output.frameKind
                  << " x=" << unsigned(output.playerX) << " oam=" << unsigned(output.video.oam[1]) << '\n';
    }
    require(output.pixels != initialImage && spriteLeft(output) == oldLeft+1,"Button did not move rendered sprite by one pixel");
    require(e.inspect(0xE000) == e.inspect(0xC000) && canonicalAddress(0xE000) == 0xC000,"WRAM echo not resolved");
    e.setTraceEnabled(false); require(e.traceSize()==0,"Capture not invalidated when disabled");
    e.stepFrame(); e.setTraceEnabled(true); require(e.traceSize()==0,"Old writer survived an unobserved interval");
    std::cout << "PASS instruction/frame stepping, synchronized views, real writer, Right→player_x→OAM→one-pixel frame movement, echo aliases\n";
}
void safetyAndBounds() {
    Engine e(16); ready(e);
    auto state = e.stateBytes();
    for (unsigned base = 0; base <= 0xFF80; base += 128) {
        e.snapshot(base);
        if (e.stateBytes() != state) { std::cerr << "Inspection diagnostic: state changed at window " << hex(base) << '\n'; break; }
    }
    require(e.stateBytes() == state,"Inspection changed emulator state");
    auto s = e.snapshot(); auto again = e.snapshot();
    require(s.registers==again.registers && s.ticks==again.ticks && s.memory==again.memory,"Paused state advanced");
    bool rejected = false; std::array<std::uint8_t,256> invalid{};
    try { e.loadRom(invalid); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && e.stateBytes()==state,"Invalid ROM damaged current emulation");
    auto target = e.ticks() + ticksPerSecond;
    const auto start = std::chrono::steady_clock::now();
    e.advanceTo(target,std::chrono::microseconds(3000));
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    require(e.ticks()<target,"UI quantum ignored budget");
    auto benchmarkStart = std::chrono::steady_clock::now();
    for (int i = 0; i < 300; ++i) { e.setButton(Button::Right,i<200); e.stepFrame(); }
    auto benchmarkUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-benchmarkStart).count();
    require(e.traceSize()==16 && e.snapshot().evictedWrites>0,"Bounded capture failed under sustained run");
    require(e.inspect(demo::player_x)<=152,"Movement escaped screen bounds");
    bool wrongOwner = false;
    std::thread other([&] { try { e.snapshot(); } catch(const std::logic_error&) { wrongOwner=true; } }); other.join();
    require(wrongOwner,"Non-owner access was permitted");
    auto otherRom = demo::rom; otherRom[0x3000]=1; e.loadRom(otherRom);
    require(!e.snapshot().teaching && addressName(0xC000,Program::Other).empty(),"Unrecognized ROM received invented semantic names");
    std::cout << "PASS pure inspection/pause, invalid load, ownership, bounded 300-frame run, unannotated-ROM safety; 3ms quantum observed " << elapsed << " us\n";
    std::cout << "MEASURE traced sustained run: " << benchmarkUs / 300.0 << " us/frame (16.74 ms hardware frame period); no real-time guarantee\n";
}
// Step until the boot program has handed over and PC is at a cartridge address.
void toCartridge(Engine& e, std::uint16_t pc) {
    for (int n = 0; n < 200000; ++n) {
        if (e.snapshot().registers.pc == pc) return;
        e.stepInstruction();
    }
    throw std::runtime_error("Boot did not reach the cartridge");
}
void postBootState() {
    // The original boot must leave the documented DMG hand-over state (Pan Docs
    // "Power Up Sequence") so ordinary cartridges start: LCD on, A = $01.
    for (bool zeroChecksum : {false, true}) {
        auto rom = demo::rom;
        if (zeroChecksum) rom[0x14D] = 0;
        Engine e; e.loadRom(rom);
        toCartridge(e, 0x0100);
        const auto s = e.snapshot();
        const std::uint16_t af = zeroChecksum ? 0x0180 : 0x01B0;
        require(s.registers.af == af && s.registers.bc == 0x0013 && s.registers.de == 0x00D8 &&
                s.registers.hl == 0x014D && s.registers.sp == 0xFFFE,"Post-boot CPU registers differ from DMG hand-over");
        require(s.video.lcdc == 0x91 && s.video.bgp == 0xFC && e.inspect(0xFF50) & 1,"Post-boot LCD/palette/boot-unmap state wrong");
        require(std::all_of(s.video.vram.begin(),s.video.vram.end(),[](auto b) { return b == 0; }),"Boot did not clear VRAM");
    }
    std::cout << "PASS boot hands over with DMG post-boot registers, LCD on, BGP, cleared VRAM, and checksum-dependent flags\n";
}
void interruptAndHalt() {
    auto rom = demo::rom;
    // Original controlled fixture: enable a pending VBlank interrupt, then halt.
    const std::uint8_t code[] = {0xF3,0x31,0xFF,0xDF,0x3E,0x01,0xE0,0x0F,0xEA,0xFF,0xFF,0xFB,0x00,0x00,0x76};
    std::copy(std::begin(code),std::end(code),rom.begin()+0x150);
    // Handler: DI, clear IE, HALT. With no interrupt enabled, the HALT can never
    // end (the LCD is on after boot, so VBlank would otherwise wake it).
    const std::uint8_t handler[] = {0xF3,0xAF,0xE0,0xFF,0x76};
    std::copy(std::begin(handler),std::end(handler),rom.begin()+0x40);
    Engine e; e.loadRom(rom);
    toCartridge(e, 0x0150);
    bool interruptWrite = false, halted = false;
    for (int n = 0; n < 40; ++n) {
        e.stepInstruction(); auto s=e.snapshot();
        for (const auto& w : s.writes) {
            if (w.address==0xDFFE || w.address==0xDFFD) {
                require(!w.instruction,"Interrupt stack write inherited a stale opcode identity");
                interruptWrite=true;
            }
        }
        if (s.lastExecuted && s.lastExecuted->pc==0x44) { halted=true; break; }
    }
    require(interruptWrite && halted,"Interrupt/HALT fixture did not run");
    auto before=e.snapshot(); auto result=e.stepInstruction(); auto after=e.snapshot();
    require(!result.executedInstruction && after.instructions==before.instructions,"HALT wait reported an opcode");
    require(after.ticks>before.ticks && after.next.pc==after.registers.pc,"HALT time/cursor not synchronized");
    std::cout << "PASS interrupt writes have no stale opcode attribution; HALT step reports no opcode and exact advanced cursor\n";
}

void graphicsDecoding() {
    // Original star tile from rom/teaching.asm (tile 2): both planes equal, so
    // every set bit decodes to color index 3.
    Engine e; ready(e);
    const auto s = e.snapshot();
    const std::array<std::uint8_t,16> star{0x18,0x18,0x18,0x18,0x7E,0x7E,0x3C,0x3C,0x3C,0x3C,0x7E,0x7E,0x18,0x18,0x18,0x18};
    require(std::equal(star.begin(),star.end(),s.video.vram.begin()+0x20),"VRAM copy does not hold the source tile bytes");
    require(e.inspect(tileAddress(2)) == star[0] && tileAddress(2) == 0x8020,"Tile address mapping wrong");
    const auto pixels = decodeTile(s.video.vram,2);
    for (int row = 0; row < 8; ++row) for (int column = 0; column < 8; ++column) {
        const bool set = (star[row*2] >> (7-column)) & 1;
        require(pixels[row*8+column] == (set ? 3 : 0),"Star tile decode wrong");
    }
    require(colorIndex(0b10000000,0b00000000,0)==1 && colorIndex(0,0b10000000,0)==2 && colorIndex(1,1,7)==3,"Bit-plane order wrong");
    require(shade(0xE4,0)==0 && shade(0xE4,1)==1 && shade(0xE4,2)==2 && shade(0xE4,3)==3 && shade(0x1B,0)==3,"Palette shade mapping wrong");
    require(backgroundTile(0x00,true)==0 && backgroundTile(0x00,false)==256 && backgroundTile(0x80,false)==128 && backgroundTile(0x7F,false)==383,"Background addressing wrong");
    const auto star0 = sprite(s.video.oam,0);
    require(star0.tile==2 && star0.screenX()==s.playerX && star0.screenY()==s.playerY && star0.onScreen(8),"OAM record parse wrong");
    require(!sprite(s.video.oam,1).onScreen(8),"Cleared OAM record reported visible");
    require(s.video.obp0==0xE4 && s.video.bgp==0xE4 && s.video.lcdc==0x93,"Video register copy wrong");
    // The inspector palette must be the colours SameBoy produced in the frame.
    const auto colors = shadeColors();
    for (auto pixel : s.pixels) require(std::find(colors.begin(),colors.end(),pixel)!=colors.end(),"Frame pixel outside the shade palette");
    require(std::count(s.pixels.begin(),s.pixels.end(),colors[3]) == std::count(pixels.begin(),pixels.end(),3),"Rendered star pixel count differs from its decoded tile");
    std::cout << "PASS tile/bit-plane/palette/OAM decoding from copied storage matches source bytes and SameBoy's rendered colours\n";
}
void watchMovementLesson() {
    Engine e; ready(e);
    auto start = e.snapshot();
    // With no input, the player_x store never executes: the watch stops at its limit.
    auto idle = e.runUntilWrite(demo::player_x,3*140448);
    require(idle.stop==WatchResult::Stop::Limit && !idle.write && idle.advancedTicks>=3*140448 && idle.frames>=2,"Watch without a write did not stop at its limit");
    require(e.snapshot().playerX==start.playerX,"Idle watch changed position");
    e.setButton(Button::Right,true);
    require(e.heldButtons()==1 && e.snapshot().heldButtons==1,"Held input not reported");
    auto before = e.snapshot();
    auto hit = e.runUntilWrite(demo::player_x,4*140448);
    auto after = e.snapshot();
    require(hit.stop==WatchResult::Stop::Write && hit.write,"Watch missed the player_x store");
    require(hit.write->instruction && hit.write->instruction->pc==demo::write_player_x_right,"Watch stopped at the wrong writer");
    require(after.registers.pc==demo::write_player_x_right+3 && after.next.pc==after.registers.pc,"Watch cursor is not the writer's successor boundary");
    require(after.playerX==before.playerX+1 && hit.write->requested==after.playerX && hit.write->after==after.playerX,"Watch event values disagree with storage");
    require(hit.write->endTicks==after.ticks && after.ticks==before.ticks+hit.advancedTicks && after.instructions==before.instructions+hit.instructions,"Watch timing/cursor mismatch");
    require(after.video.oam[1]==before.video.oam[1],"OAM changed before its store");
    auto oam = e.runUntilWrite(0xFE01,140448);
    auto copied = e.snapshot();
    require(oam.stop==WatchResult::Stop::Write && oam.write->instruction->pc==demo::write_oam_x && oam.frames==0,"OAM X watch wrong");
    require(oam.write->requested==copied.playerX+8 && copied.video.oam[1]==copied.playerX+8,"OAM X value wrong");
    require(copied.pixels==after.pixels,"Display changed before the PPU drew a new frame");
    const auto oldLeft = spriteLeft(copied);
    auto frame = e.stepFrame(); auto shown = e.snapshot();
    require(frame.completedFrame && shown.previousFrame==shown.frames-1 && shown.previousPixels==copied.pixels,"Previous-output retention wrong");
    require(spriteLeft(shown)==oldLeft+1 && shown.pixels!=shown.previousPixels,"New frame does not show the one-pixel move");
    e.setButton(Button::Right,false);
    require(e.heldButtons()==0,"Release not reported");
    // Echo addresses watch the same byte.
    e.setButton(Button::Left,true);
    auto echo = e.runUntilWrite(0xE000,4*140448);
    require(echo.stop==WatchResult::Stop::Write && echo.write->instruction->pc==demo::write_player_x_left,"Echo-address watch did not match the canonical byte");
    e.setButton(Button::Left,false);
    // Capture off: refuse without advancing.
    e.setTraceEnabled(false);
    const auto ticks = e.ticks();
    require(e.runUntilWrite(demo::player_x,140448).stop==WatchResult::Stop::CaptureOff && e.ticks()==ticks,"Watch ran without capture");
    std::cout << "PASS run-until-write: limit, Right→player_x store boundary, OAM X copy before display, next frame +1 px, echo alias, capture-off refusal\n";
}
void watchParity() {
    // The watch path must execute exactly what ordinary stepping executes.
    Engine traced, plain; ready(traced); ready(plain); plain.setTraceEnabled(false);
    require(traced.stateBytes()==plain.stateBytes(),"Ready states differ");
    traced.setButton(Button::Right,true); plain.setButton(Button::Right,true);
    for (std::uint16_t target : {demo::player_x, std::uint16_t(0xFE01), demo::player_x}) {
        auto hit = traced.runUntilWrite(target,4*140448);
        require(hit.stop==WatchResult::Stop::Write,"Parity watch missed");
        for (std::uint64_t i = 0; i < hit.instructions; ++i) require(plain.stepInstruction().executedInstruction,"Plain step missed an opcode");
        require(traced.stateBytes()==plain.stateBytes(),"Run-until-write diverged from instruction stepping");
    }
    std::cout << "PASS run-until-write reaches the identical full state as untraced instruction stepping\n";
}
void activityMapping() {
    Engine e; ActivityMap map;
    e.activityMap(map);
    require(map.writes.size()==0x10000 && map.executions.size()==0x10000 && map.startTicks==0,"Activity map shape wrong");
    ready(e);
    e.activityMap(map);
    // Initialization clears VRAM and copies tiles: real write attempts, not a model.
    require(map.writes[0x8000]>=2 && map.writes[0x9FFF]>=1 && map.writes[demo::player_x]>=1 && map.writesObserved,"Initialization writes missing");
    require(map.executions[0x0100]==1 && map.executions[0x0000]==1,"Entry/boot executions wrong");
    e.clearActivityMap(); e.setButton(Button::Right,true);
    const auto start = e.ticks();
    for (int i = 0; i < 10; ++i) e.stepFrame();
    e.activityMap(map);
    require(map.startTicks==start && map.endTicks==e.ticks(),"Activity interval wrong");
    require(map.writes[0x8000]==0 && map.writes[demo::player_x]==10 && map.writes[0xFE01]==10 && map.writes[0xFF00]==10,"Per-frame write counts wrong");
    require(map.executions[demo::write_player_x_right]==10 && map.executions[demo::write_player_x_left]==0,"Execution counts wrong");
    e.setTraceEnabled(false); e.stepFrame(); e.activityMap(map);
    require(!map.writesObserved && map.writes[demo::player_x]==0 && map.startTicks<map.endTicks,"Capture-off writes counted");
    require(map.executions[demo::write_player_x_right]==1,"Execution counts stopped while capture is off");
    std::cout << "PASS activity map: per-address CPU write attempts and opcode starts over a labeled interval; cleared on capture toggle\n";
}

// The bank demo is running its main loop with bank 1 drawn.
void readyBankDemo(Engine& e) {
    for (int i = 0; i < 16; ++i) {
        e.stepFrame();
        auto s = e.snapshot();
        if (s.frameKind == "VBlank frame" && s.video.lcdc == 0x91 && e.inspect(bankdemo::current_bank) == 1) return;
    }
    throw std::runtime_error("Bank demo did not start");
}
bool tileIs(Engine& e, std::span<const std::uint8_t> bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) if (e.inspect(std::uint16_t(0x8010 + i)) != bytes[i]) return false;
    return true;
}
std::span<const std::uint8_t> bankPattern(std::span<const std::uint8_t> rom, int bank) {
    return rom.subspan(std::size_t(bank) * 0x4000 + (bankdemo::bank1_pattern - 0x4000), 16);
}
void bankedCartridge() {
    const std::span<const std::uint8_t> rom(bankdemo::rom);
    Engine e; e.loadRom(rom);
    auto s = e.snapshot();
    const auto& info = s.cartridge.info;
    require(s.bankDemo && !s.teaching && info.mbc == Mbc::Mbc1 && info.typeName == "MBC1+RAM+BATTERY" && info.romBanks() == 4 &&
            info.battery && info.ram && info.headerRamBytes == 8192 && info.headerChecksumValid && info.title == "BANK DEMO","Header decode wrong");
    require(s.cartridge.romBytes == 65536 && s.cartridge.ramBytes == 8192 && s.cartridge.bootMapped,"Emulator cartridge buffers wrong");
    readyBankDemo(e);
    s = e.snapshot();
    require(s.cartridge.banks.rom == 1 && s.cartridge.banks.rom0 == 0 && tileIs(e, bankPattern(rom, 1)),"Bank 1 not mapped/drawn");
    for (int i = 0; i < 16; ++i) require(e.inspect(std::uint16_t(0x4000 + i)) == rom[0x4000 + i],"Window $4000 is not bank 1 storage");
    // Press A: the program writes 2 to the MBC1 bank register.
    e.setButton(Button::A,true);
    auto change = e.runUntilBankChange(4 * 140448);
    require(change.stop == WatchResult::Stop::BankChange && change.banksBefore.rom == 1 && change.banksAfter.rom == 2,"Bank change not observed");
    require(change.write && change.write->address == 0x2000 && change.write->requested == 2 && change.write->instruction &&
            change.write->instruction->pc == bankdemo::write_rom_bank && change.write->banksBefore.rom == 1 && change.write->banksAfter.rom == 2,
            "Bank change lacks its MBC writer evidence");
    s = e.snapshot();
    require(s.cartridge.banks.rom == 2 && s.next.pc == bankdemo::call_bank,"Cursor not after the bank-select write");
    for (int i = 0; i < 64; ++i) require(e.inspect(std::uint16_t(0x4000 + i)) == rom[2 * 0x4000 + i],"Window $4000 does not show bank 2 storage");
    require(!tileIs(e, bankPattern(rom, 2)),"Tile changed before bank 2's routine ran");
    // The CALL lands in bank 2's routine: same address, different bytes.
    for (int n = 0; n < 4 && e.snapshot().next.pc != 0x4000; ++n) e.stepInstruction();
    s = e.snapshot();
    require(s.next.pc == 0x4000 && s.next.bank == 2 && s.next.bytes[0] == rom[2 * 0x4000],"Next instruction not from bank 2");
    e.stepInstruction();
    require(e.snapshot().lastExecuted->bank == 2,"Executed opcode not attributed to bank 2");
    // Cartridge RAM: enable, increment, disable.
    auto save = e.runUntilWrite(bankdemo::saved_count, 140448);
    require(save.stop == WatchResult::Stop::Write && save.write->physicalStorage && save.write->before == 0 &&
            save.write->requested == 1 && save.write->after == 1 && save.write->bank == 0 && e.inspect(0xA000) == 1,"Cartridge RAM write not observed");
    e.stepFrame();
    require(tileIs(e, bankPattern(rom, 2)),"Bank 2's pattern not drawn");
    // Three more presses: 3, 1 (wraps), 2.
    for (int expected : {3, 1, 2}) {
        e.setButton(Button::A,false); e.stepFrame(); e.setButton(Button::A,true);
        auto next = e.runUntilBankChange(4 * 140448);
        require(next.stop == WatchResult::Stop::BankChange && next.banksAfter.rom == expected,"Bank sequence wrong");
        e.stepFrame(); e.stepFrame();
    }
    e.setButton(Button::A,false);
    require(e.inspect(bankdemo::saved_count) == 4 && tileIs(e, bankPattern(rom, 2)),"Saved count or pattern wrong after four switches");
    ActivityMap map; e.activityMap(map);
    const auto cartridgeOpcodes = std::accumulate(map.executions.begin(), map.executions.begin() + 0x8000, std::uint64_t{});
    const auto perBank = std::accumulate(map.bankExecutions.begin(), map.bankExecutions.end(), std::uint64_t{});
    require(map.bankExecutions.size() == 4 && map.bankExecutions[1] && map.bankExecutions[2] && map.bankExecutions[3] &&
            perBank + map.bootExecutions == cartridgeOpcodes && map.romBankChanges == 4,"Per-bank execution accounting wrong");
    // Inspecting every window with bank 2 mapped and RAM present changes nothing.
    const auto state = e.stateBytes();
    for (unsigned base = 0; base <= 0xFF80; base += 128) e.snapshot(std::uint16_t(base));
    require(e.stateBytes() == state,"Banked inspection changed emulator state");
    // Battery: a power cycle keeps cartridge RAM; a fresh engine can load the .sav bytes.
    const auto battery = e.batteryData();
    require(battery.size() == 8192 && battery[0] == 4 && battery[1] == 0x42,"Battery data wrong");
    e.restart(); readyBankDemo(e);
    require(e.inspect(bankdemo::saved_count) == 4,"Restart lost battery-backed RAM");
    Engine fresh; fresh.loadRom(rom); fresh.loadBattery(battery); readyBankDemo(fresh);
    require(fresh.inspect(bankdemo::saved_count) == 4,"Loaded battery data not visible");
    std::cout << "PASS MBC1 cartridge: header decode, bank-aware windows and disassembly, bank-change stop with MBC writer, banked code, "
                 "cartridge RAM evidence, per-bank counts, pure inspection, battery across restart and reload\n";
}
void bankParityAndOtherCartridges() {
    // Tracing must not change banked execution either.
    Engine traced, plain; plain.setTraceEnabled(false);
    traced.loadRom(bankdemo::rom); plain.loadRom(bankdemo::rom);
    readyBankDemo(traced); readyBankDemo(plain);
    for (int press = 0; press < 3; ++press) {
        traced.setButton(Button::A,true); plain.setButton(Button::A,true);
        auto a = traced.runUntilBankChange(4 * 140448), b = plain.runUntilBankChange(4 * 140448);
        require(a.stop == b.stop && a.banksAfter == b.banksAfter && a.instructions == b.instructions && !b.write,"Bank-change watch differs with tracing");
        require(traced.stateBytes() == plain.stateBytes(),"Tracing changed banked execution");
        traced.setButton(Button::A,false); plain.setButton(Button::A,false);
        traced.stepFrame(); plain.stepFrame();
    }
    // The same program as an MBC5 cartridge padded to 128 KiB: other controller, more banks.
    std::vector<std::uint8_t> mbc5(bankdemo::rom.begin(), bankdemo::rom.end());
    mbc5.resize(128 * 1024, 0xFF);
    mbc5[0x147] = 0x1B; mbc5[0x148] = 0x02;
    Engine e; e.loadRom(mbc5); readyBankDemo(e);
    auto s = e.snapshot();
    require(s.cartridge.info.mbc == Mbc::Mbc5 && s.cartridge.info.romBanks() == 8 && !s.cartridge.info.headerChecksumValid && !s.bankDemo,"MBC5 header decode wrong");
    e.setButton(Button::A,true);
    auto change = e.runUntilBankChange(4 * 140448);
    require(change.stop == WatchResult::Stop::BankChange && change.banksAfter.rom == 2 && e.inspect(0x4000) == mbc5[2 * 0x4000],"MBC5 bank switch wrong");
    // Accept any size SameBoy can map; reject non-cartridges without touching the current one.
    const auto before = e.stateBytes();
    bool tooSmall = false, tooLarge = false;
    try { std::vector<std::uint8_t> tiny(0x14F); e.loadRom(tiny); } catch (const std::invalid_argument&) { tooSmall = true; }
    try { std::vector<std::uint8_t> huge(maxRomBytes + 1); e.loadRom(huge); } catch (const std::invalid_argument&) { tooLarge = true; }
    require(tooSmall && tooLarge && e.stateBytes() == before,"ROM size validation wrong");
    // Header facts for cartridges this demo cannot exercise.
    auto variant = [](std::uint8_t type, std::uint8_t cgb, std::size_t size) {
        std::vector<std::uint8_t> rom(demo::rom.begin(), demo::rom.end());
        rom.resize(size, 0xFF); rom[0x147] = type; rom[0x143] = cgb;
        return describeCartridge(rom);
    };
    require(variant(0x00, 0x00, 65536).mbc == Mbc::Mbc3 && !variant(0x00, 0x00, 65536).note.empty(),"No-MBC oversize heuristic wrong");
    require(!variant(0x55, 0x00, 32768).supported && variant(0x55, 0x00, 32768).mbc == Mbc::Unknown,"Unknown type not flagged");
    require(variant(0x13, 0xC0, 32768).cgbOnly() && variant(0x13, 0x80, 32768).cgbEnhanced() && variant(0x13, 0, 32768).timer == false &&
            variant(0x10, 0, 32768).timer && variant(0x20, 0, 32768).supported == false,"CGB flags or controller features wrong");
    require(mbcRegisterName(Mbc::Mbc1, 0x2000).find("ROM bank") != std::string::npos && mbcRegisterName(Mbc::None, 0x2000).empty(),"MBC register names wrong");
    std::cout << "PASS banked trace parity, MBC5 at 128 KiB, ROM size validation, header heuristics, CGB flags, MBC register names\n";
}
void oamDma() {
    // The request is observed; the copy is checked against SameBoy's own timing.
    Engine e; e.loadRom(dmaFixture()); toCartridge(e, 0x150);
    const auto before = e.snapshot().video.oam;
    require(before[159] != 159,"Fixture needs OAM's last byte to change");
    auto request = e.runUntilWrite(0xFF46, 140448);
    require(request.stop == WatchResult::Stop::Write && request.write->requested == 0xC1 && request.write->instruction &&
            request.write->instruction->pc == 0xFF82,"DMA request write not captured from HRAM");
    const auto requestEnd = e.ticks();
    auto copying = e.snapshot();
    require(copying.dmaCopying && copying.dmaCopying->page == 0xC1 && copying.dmaCopying->before == before && copying.dma.empty(),
            "DMA in progress not published");
    // Step until the last OAM byte arrives; the check must not come earlier.
    std::uint64_t lastByte = 0;
    while (!lastByte && e.ticks() - requestEnd < 4000) {
        e.stepInstruction();
        if (e.inspect(0xFE9F) == 159) lastByte = e.ticks();
    }
    require(lastByte && lastByte - requestEnd > 1200 && lastByte <= requestEnd + 1296,"DMA duration differs from the checked window");
    while (e.snapshot().dma.empty() && e.ticks() - requestEnd < 4000) e.stepInstruction();
    auto s = e.snapshot();
    require(s.dma.size() == 1 && !s.dmaCopying,"DMA check missing");
    const auto& t = s.dma.back();
    require(t.status == DmaTransfer::Status::Checked && t.matching == 160 && t.page == 0xC1 && t.sourceOf(5) == 0xC105 &&
            t.instruction && t.instruction->pc == 0xFF82 && t.requestEndTicks == requestEnd &&
            t.checkedTicks >= requestEnd + 1296 && t.checkedTicks < requestEnd + 1296 + 64,"DMA record wrong");
    for (std::size_t i = 0; i < oamBytes; ++i) require(t.after[i] == i && s.video.oam[i] == i,"OAM not equal to the copied source");
    require(t.changed() == int(std::count_if(before.begin(), before.end(), [i = 0](auto b) mutable { return b != i++; })),"Changed-byte count wrong");
    // Run until written on OAM stops at the next frame's checked copy, which moved sprite 0.
    auto next = e.runUntilWrite(0xFE01, 2 * 140448);
    require(next.stop == WatchResult::Stop::Dma && next.dma && next.dma->after[1] == 2 && next.dma->before[1] == 1 &&
            next.dma->changed() == 1 && e.inspect(0xFE01) == 2,"Run until OAM written did not stop at the DMA check");
    // Restart: a second request before the first finished.
    Engine r; r.loadRom(dmaFixture(true)); toCartridge(r, 0x150);
    r.runUntilWrite(0xFE00, 0xFE9F, 140448);
    s = r.snapshot();
    require(s.dma.size() == 2 && s.dma[0].status == DmaTransfer::Status::Restarted && s.dma[1].status == DmaTransfer::Status::Checked &&
            s.dma[1].matching == 160 && s.dma[1].instruction->pc == 0xFF84,"Restarted DMA not recorded");
    // Tracing changes nothing; without capture there are no DMA records.
    Engine traced, plain; plain.setTraceEnabled(false);
    traced.loadRom(dmaFixture()); plain.loadRom(dmaFixture());
    std::size_t checked = 0;
    for (int frame = 0; frame < 12; ++frame) {
        traced.stepFrame(); plain.stepFrame();
        for (const auto& d : traced.snapshot().dma) checked += d.matching == 160;
        require(plain.snapshot().dma.empty(),"DMA recorded with capture off");
    }
    require(traced.stateBytes() == plain.stateBytes() && traced.ticks() == plain.ticks(),"DMA observation changed emulation");
    require(checked >= 10,"Per-frame DMA copies not all verified");
    std::cout << "PASS OAM DMA: request captured from HRAM, copy timing within the checked window, OAM equals source, "
                 "run-until-written stops at the copy, restart, parity, capture-off\n";
}
void decode() {
    require(disassemble({0x200,{0xEA,0x00,0xC0}})=="LD [$C000], A","LD disassembly wrong");
    require(disassemble({0x200,{0xCB,0x47,0}})=="BIT 0, A","CB disassembly wrong");
    require(disassemble({0x200,{0x20,0xFC,0}})=="JR NZ, $01FE","Relative disassembly wrong");
    require(instructionLength(0xEA)==3 && instructionLength(0xCB)==2 && instructionLength(0x76)==1,"Instruction length wrong");
}
void reviewRegressions() {
    // Failed loads must preserve the entire machine, not only its registers.
    Engine e; e.loadRom(bankdemo::rom); readyBankDemo(e);
    const auto before = e.stateBytes();
    auto unsupported = std::vector<std::uint8_t>(demo::rom.begin(), demo::rom.end());
    unsupported[0x147] = 0x55;
    bool rejected = false;
    try { e.loadRom(unsupported); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && e.stateBytes() == before && e.snapshot().bankDemo, "Unsupported cartridge replaced the current game");
    const auto dirty = e.batteryDirty();
    for (const std::size_t size : {0u, 8191u, 8193u, 8192u + 48u}) {
        require(!e.loadBattery(std::vector<std::uint8_t>(size, 0x42)) && e.stateBytes() == before && e.batteryDirty() == dirty,
                "Malformed battery save changed state");
    }
    require(e.loadBattery(std::vector<std::uint8_t>(8192, 0x33)) && e.inspect(0xA000) == 0x33 && !e.batteryDirty(), "Valid RAM save was rejected");
    auto clock = unsupported; clock[0x147] = 0x10; clock[0x149] = 2;
    e.loadRom(clock);
    require(e.batteryData().size() == 8192 + 48, "Native clock save size wrong");
    for (const std::size_t footer : {std::size_t(0), 5 + sizeof(time_t), std::size_t(44), std::size_t(48)}) {
        require(e.loadBattery(std::vector<std::uint8_t>(8192 + footer, 0)), "Recognized RTC save format was rejected");
    }
    const auto clockBefore = e.stateBytes();
    require(!e.loadBattery(std::vector<std::uint8_t>(8193)) && e.stateBytes() == clockBefore, "Short RTC footer was accepted");
    for (const std::uint8_t type : {0x06, 0x09, 0x13, 0x1B, 0x22, 0xFC, 0xFE}) {
        auto batteryRom = unsupported; batteryRom[0x147] = type; batteryRom[0x149] = 2;
        e.loadRom(batteryRom);
        const auto data = e.batteryData();
        require(!data.empty() && e.loadBattery(data), "Native supported-controller save failed to round-trip");
    }
    auto tpp1 = unsupported;
    tpp1[0x147] = 0xBC; tpp1[0x149] = 0xC1; tpp1[0x14A] = 0x65; tpp1[0x152] = 1; tpp1[0x153] = 8;
    e.loadRom(tpp1);
    require(e.batteryData().size() == 8192 + 20 && e.loadBattery(e.batteryData()), "TPP1 clock save failed to round-trip");
    // The trailing MMM01 header wins over oversized ROM-only detection.
    e.loadRom(multicartFixture());
    auto s = e.snapshot();
    require(s.cartridge.info.mbc == Mbc::Mmm01 && s.cartridge.info.type == 0 && s.cartridge.info.effectiveType == 0x0D &&
            s.cartridge.info.battery && s.cartridge.ramBytes == 8192 && e.batteryData().size() == 8192 && s.cartridge.banks.rom0 == 2,
            "Multicart metadata or battery disagrees with the core");
    e.loadRom(multicartFixture(0x11));
    require(e.snapshot().cartridge.info.mbc == Mbc::Mmm01 && e.batteryData().empty(), "MBC3-labelled MMM01 detection wrong");
    auto contradictory = unsupported; contradictory[0x147] = 0x0F; contradictory[0x149] = 2;
    e.loadRom(contradictory); s = e.snapshot();
    require(s.cartridge.info.effectiveType == 0x10 && s.cartridge.info.ram && s.cartridge.info.timer &&
            s.cartridge.ramBytes == 8192 && e.batteryData().size() == 8192 + 48, "RAM recovery lost controller features");
    // Use the actual writes and the same storage predicate used by the UI.
    e.loadRom(bankedRamFixture());
    toCartridge(e, 0x150);
    for (int i = 0; i < 14; ++i) e.stepInstruction();
    s = e.snapshot(0xA000);
    const auto w = std::find_if(s.writes.rbegin(), s.writes.rend(), [&](const auto& event) { return writerMatches(event, 0xA000, s.cartridge); });
    require(s.cartridge.banks.ram == 0 && s.memory[0] == 0x11 && w != s.writes.rend() && w->bank == 0 && w->after == 0x11,
            "Last writer came from another RAM bank");
    require(e.batteryDirty(), "RAM writes did not mark progress dirty");
    e.restart();
    require(e.batteryDirty() && e.inspect(0xA000) == 0x11, "Restart forgot unsaved battery progress");
    CartridgeState small; small.ramBytes = 2048;
    WriteEvent mirror; mirror.address = mirror.canonicalAddress = 0xA800; mirror.bank = 3;
    require(writerMatches(mirror, 0xA000, small), "Small cartridge RAM mirrors do not share writer evidence");
    const auto wrap = std::uint64_t(2199023255552);
    require(ticksForNanoseconds(wrap) >= ticksForNanoseconds(wrap - 1) &&
            ticksForNanoseconds(3600ull * 1000000000) == 3600ull * ticksPerSecond &&
            ticksForNanoseconds(24ull * 3600 * 1000000000) == 24ull * 3600 * ticksPerSecond, "Long-running pacing target wrapped");
    std::cout << "PASS review regressions: transactional ROM/save validation, legacy RTC buffers, multicart battery, RAM recovery, bank-aware writers, long-run pacing\n";
}
int main() {
    try { decode(); postBootState(); parity(); steppingAndMovement(); safetyAndBounds(); interruptAndHalt(); graphicsDecoding(); watchMovementLesson(); watchParity(); activityMapping(); bankedCartridge(); bankParityAndOtherCartridges(); oamDma(); reviewRegressions(); }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    return 0;
}
