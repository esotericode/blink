#include "ui/main_window.hpp"
#include "ui/game_view.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace observatory;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    QApplication app(argc,argv);
    MainWindow window; window.show(); QTest::qWait(50);
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
        require(initial.frameKind=="VBlank frame" && initial.sprite[1]==80,"GUI stopped on a startup transition output");
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
        if (argc>1) require(window.grab().save(QString::fromLocal8Bit(argv[1])),"Screenshot could not be saved");
        if (argc>2) {
            window.resize(1040,790); QTest::qWait(30);
            require(window.grab().save(QString::fromLocal8Bit(argv[2])),"Minimum-size screenshot could not be saved");
        }
        std::cout << "PASS Qt " << qVersion() << " / " << qPrintable(QGuiApplication::platformName())
                  << ": rendered native widgets, input/focus, run/pause, instruction/frame actions, synchronized inspectors, writer selection; UI heartbeats=" << heartbeats << "\n";
    } catch(const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    return 0;
}
