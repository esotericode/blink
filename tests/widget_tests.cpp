#include "ui/activity_map.hpp"
#include "ui/game_view.hpp"
#include "ui/main_window.hpp"
#include "ui/system_diagram.hpp"
#include "ui/tile_view.hpp"
#include "teaching_rom.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace observatory;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

int main(int argc, char** argv) {
    QApplication app(argc,argv);
    MainWindow window; window.show(); QTest::qWait(50);
    // Optional third argument: a directory for screenshots of each lesson step and inspector tab.
    const QString shots = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString();
    auto shot = [&](const QString& name) {
        if (!shots.isEmpty()) require(window.grab().save(QDir(shots).filePath(name)),"Screenshot could not be saved");
    };
    try {
        auto* game=window.findChild<GameView*>("gameView");
        auto* regs=window.findChild<QTableWidget*>("registerTable");
        auto* memory=window.findChild<QTableWidget*>("memoryTable");
        auto* run=window.findChild<QAction*>("runAction");
        auto* instruction=window.findChild<QAction*>("instructionAction");
        auto* frame=window.findChild<QAction*>("frameAction");
        auto* edit=window.findChild<QLineEdit*>("memoryAddress");
        auto* writer=window.findChild<QLabel*>("writerLabel");
        require(game && regs && memory && run && instruction && frame && edit && writer,"Required native controls absent");
        require(!game->image().isNull() && window.displayedSnapshot().playerX==72,"GUI teaching output not ready");
        auto initial=window.displayedSnapshot();
        require(initial.frameKind=="VBlank frame" && initial.video.oam[1]==80,"GUI stopped on a startup transition output");
        instruction->trigger(); auto stepped=window.displayedSnapshot();
        require(!window.running() && stepped.instructions==initial.instructions+1,"GUI instruction action wrong");
        require(regs->item(0,5)->text().startsWith(QString::fromStdString(hex(stepped.registers.pc))),"Register widget stale");
        require(memory->item(0,1)->text().startsWith(QString::fromStdString(hex(stepped.playerX,2)).mid(1)),"Memory widget stale");
        require(window.findChild<QLabel*>("instructionLabel")->text().contains(QString::fromStdString(hex(stepped.next.pc))),"Instruction widget stale");
        frame->trigger(); require(window.displayedSnapshot().frames==stepped.frames+1,"GUI frame action wrong");
        edit->setFocus(); edit->setText("FE00"); QTest::keyClick(edit,Qt::Key_Return);
        require(window.displayedSnapshot().memoryBase==0xFE00,"Memory address editor did not work");
        window.setMemoryBase(0xC000);
        // A game key must reach the core even while a table owns focus.
        memory->setFocus(); QTest::keyPress(memory,Qt::Key_Right); frame->trigger(); frame->trigger(); QTest::keyRelease(memory,Qt::Key_Right);
        require(window.displayedSnapshot().playerX>72,"Focused table swallowed directional game input");
        QTest::mouseClick(memory->viewport(),Qt::LeftButton,Qt::NoModifier,memory->visualItemRect(memory->item(0,1)).center());
        require(window.selectedAddress()==0xC000 && writer->text().contains("LD [$C000], A"),"Memory selection did not follow real writer");
        require(writer->text().contains("Right increments player_x"),"Source annotation missing");
        auto* writes=window.findChild<QTableWidget*>("writeTable");
        int oamRow=-1;
        for (int row=0;row<writes->rowCount();++row) if (writes->item(row,2)->text().startsWith("$FE01")) { oamRow=row; break; }
        require(oamRow>=0,"OAM write missing from rendered trace");
        require(writes->item(oamRow,1)->text().contains("LD [$FE01], A"),"Write list lacks the writer's disassembly");
        QTest::mouseClick(writes->viewport(),Qt::LeftButton,Qt::NoModifier,writes->visualItemRect(writes->item(oamRow,2)).center());
        require(window.displayedSnapshot().memoryBase==0xFE00 && window.selectedAddress()==0xFE01,"Selecting a write did not select its memory byte");
        // Verify that emulation yields to a separate UI timer while running.
        int heartbeats=0; QTimer heartbeat; heartbeat.setInterval(5);
        QObject::connect(&heartbeat,&QTimer::timeout,[&] { ++heartbeats; }); heartbeat.start();
        auto tickBefore=window.displayedSnapshot().ticks; run->trigger(); QTest::qWait(250); run->trigger(); heartbeat.stop();
        require(!window.running() && window.displayedSnapshot().ticks>tickBefore,"Run/pause action failed");
        require(heartbeats>=10,"UI timer starved during emulation");
        auto paused=window.displayedSnapshot(); QTest::qWait(50);
        require(window.engine().ticks()==paused.ticks,"Pause did not stop emulator");
        // Releasing focus must release held input.
        game->setFocus(); QTest::keyPress(game,Qt::Key_Right);
        QEvent deactivate(QEvent::WindowDeactivate); QApplication::sendEvent(&window,&deactivate);
        frame->trigger(); frame->trigger(); require(window.displayedSnapshot().playerX==paused.playerX,"Focus loss left a stuck game button");
        window.findChild<QPushButton*>("inspectMovementButton")->click();
        require(window.selectedAddress()==0xC000,"Movement shortcut did not select player_x");

        // Break on write for any byte: frame_counter is stored once per game update.
        const auto counter=window.displayedSnapshot().memory[3];
        auto watch=window.runUntilWritten(demo::frame_counter);
        require(watch.stop==WatchResult::Stop::Write && watch.write->requested==std::uint8_t(counter+1),"Run until written missed frame_counter");
        require(window.displayedSnapshot().memory[3]==std::uint8_t(counter+1) && window.selectedAddress()==demo::frame_counter,"Run until written left inspectors stale");

        // Guided lesson through its real button: every stop is a real emulator event.
        window.loadTeaching();
        window.resize(1280,930); window.resetLayout(); QTest::qWait(30);
        auto* action=window.findChild<QPushButton*>("primaryAction");
        auto* body=window.findChild<QLabel*>("lessonBody");
        auto* diagram=window.findChild<SystemDiagram*>("systemDiagram");
        auto* tiles=window.findChild<TileInspector*>("tileInspector");
        auto* tilesDock=window.findChild<QDockWidget*>("tilesDock");
        require(action && body && diagram && tiles && tilesDock,"Lesson controls absent");
        require(window.lessonStep()==LessonStep::Start && action->text().contains("Hold Right"),"Lesson did not start ready");
        shot("lesson-0-start.png");
        const auto start=window.displayedSnapshot();
        action->click();
        require(window.lessonStep()==LessonStep::Holding && window.displayedSnapshot().heldButtons==1 && window.displayedSnapshot().ticks==start.ticks,
                "Hold step should hold Right without running");
        require(diagram->highlightedPaths().count(SystemDiagram::Path::JoypadCpu)==1,"Diagram does not show the joypad path");
        shot("lesson-1-joypad.png");
        action->click();
        const auto stored=window.displayedSnapshot();
        require(window.lessonStep()==LessonStep::Stored && stored.playerX==start.playerX+1,"Lesson did not stop at the player_x store");
        require(stored.registers.pc==demo::write_player_x_right+3 && window.selectedAddress()==demo::player_x,"Lesson cursor/selection wrong after store");
        require(stored.frames==start.frames && stored.pixels==start.pixels,"Picture changed before the store's frame (lesson text would be wrong)");
        require(body->text().contains("LD [$C000], A") && writer->text().contains("LD [$C000], A"),"Lesson/writer text lacks the real instruction");
        require(diagram->highlightedPaths().count(SystemDiagram::Path::CpuWram)==1,"Diagram does not show the CPU→WRAM path");
        shot("lesson-2-wram.png");
        action->click();
        const auto copied=window.displayedSnapshot();
        require(window.lessonStep()==LessonStep::Copied && copied.video.oam[1]==copied.playerX+8 && copied.frames==stored.frames,"Lesson did not stop at the OAM X store");
        require(game->showSprites() && game->selectedSprite()==0 && body->text().contains("LD [$FE01], A"),"OAM step overlay/text wrong");
        shot("lesson-3-oam.png");
        action->click();
        const auto drawn=window.displayedSnapshot();
        require(window.lessonStep()==LessonStep::Drawn && drawn.frames==copied.frames+1 && drawn.previousFrame==copied.frames,"Lesson frame step wrong");
        require(game->showChanges() && game->changedPixels()>0 && game->changedPixels()<64,"Changed-pixel overlay does not show the star's move");
        require(body->text().contains(QString("%1 pixels").arg(game->changedPixels())),"Lesson text does not report the measured change");
        shot("lesson-4-frame.png");
        action->click();
        require(window.lessonStep()==LessonStep::Tile && !tilesDock->visibleRegion().isEmpty() && tiles->selectedSprite()==0 && tiles->selectedTile()==2,
                "Tile step did not show sprite 0's tile");
        auto* oam=window.findChild<QTableWidget*>("oamTable");
        require(oam && oam->item(0,2)->text()==QString::number(window.displayedSnapshot().video.oam[1]) && oam->item(0,3)->text()=="2","OAM table stale");
        shot("lesson-5-tile.png");
        action->click();
        require(window.lessonStep()==LessonStep::Done && window.displayedSnapshot().heldButtons==0,"Lesson did not finish and release Right");

        // Memory map: real counts over a labelled interval, cleared on request.
        auto* mapDock=window.findChild<QDockWidget*>("mapDock");
        mapDock->raise(); QTest::qWait(20);
        auto* interval=window.findChild<QLabel*>("activityInterval");
        require(interval && interval->text().contains("write attempts") && interval->text().contains("not counted"),"Activity map is not labelled");
        shot("tab-memory-map.png");
        window.findChild<QPushButton*>("activityClear")->click();
        ActivityMap map; window.engine().activityMap(map);
        require(map.startTicks==window.engine().ticks() && map.writes[demo::player_x]==0,"Clearing the activity map failed");
        // Selecting an address in the map selects it in the memory panel.
        emit window.findChild<ActivityMapView*>("activityMap")->addressActivated(0xFF00);
        require(window.selectedAddress()==0xFF00,"Activity map selection not linked");
        window.findChild<QDockWidget*>("writesDock")->raise(); QTest::qWait(20);
        shot("tab-captured-writes.png");

        // Another ROM: hardware views remain, game names and the lesson do not.
        QTemporaryDir dir; auto modified=demo::rom; modified[0x3000]=1;
        const auto path=dir.filePath("other.gb");
        { QFile f(path); require(f.open(QIODevice::WriteOnly) && f.write(reinterpret_cast<const char*>(modified.data()),modified.size())==qint64(modified.size()),"Fixture write failed"); }
        window.loadFile(path);
        require(!window.displayedSnapshot().teaching && window.lessonStep()==LessonStep::NeedsTeachingRom && action->text().contains("teaching ROM"),"Unannotated ROM lesson state wrong");
        action->click();
        require(window.displayedSnapshot().teaching && window.lessonStep()==LessonStep::Start,"Lesson could not reload the teaching ROM");

        // Screenshots for documentation: mid-lesson, where the outline leads the picture.
        if (argc>1) {
            window.resize(1280,930); window.resetLayout(); QTest::qWait(30);
            action->click(); action->click(); action->click();
            require(window.lessonStep()==LessonStep::Copied,"Screenshot lesson state wrong");
            QTest::qWait(30);
            require(window.grab().save(QString::fromLocal8Bit(argv[1])),"Screenshot could not be saved");
            window.stopLesson();
        }
        if (argc>2) {
            window.resize(window.minimumSize()); window.resetLayout(); QTest::qWait(30);
            require(window.grab().save(QString::fromLocal8Bit(argv[2])),"Minimum-size screenshot could not be saved");
        }
        std::cout << "PASS Qt " << qVersion() << " / " << qPrintable(QGuiApplication::platformName())
                  << ": native widgets, input/focus, run/pause, instruction/frame, run-until-written, synchronized inspectors, writer selection, "
                     "guided lesson (hold → WRAM store → OAM store → frame → tile), tile/OAM/activity panels; UI heartbeats=" << heartbeats << "\n";
    } catch(const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    return 0;
}
