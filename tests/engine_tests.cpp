#include "emulator/engine.hpp"
#include "emulator/graphics.hpp"
#include "teaching/annotations.hpp"
#include "teaching_rom.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
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
    require(!e.snapshot().teaching && addressName(0xC000,false).empty(),"Unrecognized ROM received invented semantic names");
    std::cout << "PASS pure inspection/pause, invalid load, ownership, bounded 300-frame run, unannotated-ROM safety; 3ms quantum observed " << elapsed << " us\n";
    std::cout << "MEASURE traced sustained run: " << benchmarkUs / 300.0 << " us/frame (16.74 ms hardware frame period); no real-time guarantee\n";
}
void interruptAndHalt() {
    auto rom = demo::rom;
    // Original controlled fixture: enable a pending VBlank interrupt, then halt.
    const std::uint8_t code[] = {0xF3,0x31,0xFF,0xDF,0x3E,0x01,0xE0,0x0F,0xEA,0xFF,0xFF,0xFB,0x00,0x00,0x76};
    std::copy(std::begin(code),std::end(code),rom.begin()+0x150);
    rom[0x40]=0xF3; rom[0x41]=0x76;
    Engine e; e.loadRom(rom);
    bool interruptWrite = false, halted = false;
    for (int n = 0; n < 40; ++n) {
        e.stepInstruction(); auto s=e.snapshot();
        for (const auto& w : s.writes) {
            if (w.address==0xDFFE || w.address==0xDFFD) {
                require(!w.instruction,"Interrupt stack write inherited a stale opcode identity");
                interruptWrite=true;
            }
        }
        if (s.lastExecuted && s.lastExecuted->pc==0x41) { halted=true; break; }
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
void decode() {
    require(disassemble({0x200,{0xEA,0x00,0xC0}})=="LD [$C000], A","LD disassembly wrong");
    require(disassemble({0x200,{0xCB,0x47,0}})=="BIT 0, A","CB disassembly wrong");
    require(disassemble({0x200,{0x20,0xFC,0}})=="JR NZ, $01FE","Relative disassembly wrong");
    require(instructionLength(0xEA)==3 && instructionLength(0xCB)==2 && instructionLength(0x76)==1,"Instruction length wrong");
}
int main() {
    try { decode(); parity(); steppingAndMovement(); safetyAndBounds(); interruptAndHalt(); graphicsDecoding(); watchMovementLesson(); watchParity(); activityMapping(); }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    return 0;
}
