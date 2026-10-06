#include "ui/activity_map.hpp"
#include "ui/cartridge_view.hpp"
#include "ui/game_view.hpp"
#include "ui/information_panel.hpp"
#include "ui/tile_map_view.hpp"
#include "ui/main_window.hpp"
#include "ui/system_diagram.hpp"
#include "ui/tile_view.hpp"
#include "ui/tooltip.hpp"
#include "teaching_rom.hpp"
#include "fixtures.hpp"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextBrowser>
#include <QScrollBar>
#include <QComboBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QScreen>
#include <QScrollArea>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <iostream>
#include <stdexcept>

using namespace observatory;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

int main(int argc, char** argv) {
    QApplication app(argc,argv);
    // Battery saves of the bundled demo go to a throwaway test location.
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/bankdemo.sav");
    MainWindow window; window.show(); QTest::qWait(50);
    // Optional third argument: a directory for screenshots of contextual information and inspector tabs.
    const QString shots = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString();
    auto shot = [&](const QString& name) {
        if (shots.isEmpty()) return;
        QTest::qWait(30); // let pending layout requests run, as they would on screen
        require(window.grab().save(QDir(shots).filePath(name)),"Screenshot could not be saved");
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
        window.selectAddress(demo::player_x);
        require(window.selectedAddress()==0xC000,"Movement shortcut did not select player_x");

        // Break on write for any byte: frame_counter is stored once per game update.
        const auto counter=window.displayedSnapshot().memory[3];
        auto watch=window.runUntilWritten(demo::frame_counter);
        require(watch.stop==WatchResult::Stop::Write && watch.write->requested==std::uint8_t(counter+1),"Run until written missed frame_counter");
        require(window.displayedSnapshot().memory[3]==std::uint8_t(counter+1) && window.selectedAddress()==demo::frame_counter,"Run until written left inspectors stale");

        // Selection-driven learning: browsing observes without executing or
        // changing the core, and explanations are independent of ROM identity.
        window.loadTeaching();
        window.resize(1280,930); window.resetLayout(); QTest::qWait(30);
        auto* info = window.findChild<InformationPanel*>("informationPanel");
        auto* body = window.findChild<QTextBrowser*>("informationBody");
        auto* infoFacts = window.findChild<QLabel*>("informationFacts");
        auto* infoDock = window.findChild<QDockWidget*>("informationDock");
        auto* diagram = window.findChild<SystemDiagram*>("systemDiagram");
        auto* tiles = window.findChild<TileInspector*>("tileInspector");
        auto* tilesDock = window.findChild<QDockWidget*>("tilesDock");
        auto* graphicsTabs = window.findChild<QTabWidget*>("graphicsTabs");
        require(info && body && infoFacts && infoDock && diagram && tiles && tilesDock && graphicsTabs, "Information/graphics controls absent");
        require(!window.findChild<QPushButton*>("primaryAction") && !window.findChild<QDockWidget*>("lessonDock") &&
                !window.findChild<QAction*>("lessonMenuAction"), "Tutorial controls remain in the application");
        require(info->selection().topic == "explore" && window.findChild<QComboBox*>("informationTopics")->count() >= 35,
                "The reference has no usable entry point");
        shot("info-0-explore.png");
        const auto readingState = window.engine().stateBytes();
        window.selectAddress(0xC000);
        require(info->selection().topic == "wram" && body->toPlainText().contains("Work RAM") &&
                body->toPlainText().contains("How it works") && body->toPlainText().contains("Reading this inspector") &&
                body->toPlainText().size() > 1200 && infoFacts->text().contains("$C000"), "WRAM selection lacks a detailed explanation");
        shot("info-1-memory.png");
        emit body->anchorClicked(QUrl("topic:memory"));
        require(info->selection().topic == "memory" && body->toPlainText().contains("$FEA0") && body->toPlainText().contains("little-endian"),
                "Related memory reference is incomplete");
        window.findChild<QPushButton*>("informationBack")->click();
        require(info->selection().topic == "wram" && info->selection().address == 0xC000, "Reading history lost the selected address");
        window.findChild<QPushButton*>("informationForward")->click();
        require(info->selection().topic == "memory", "Forward reading history failed");
        window.selectAddress(0xFF46);
        require(info->selection().topic == "dma" && body->toPlainText().contains("160 bytes") && body->toPlainText().contains("not proof"),
                "DMA register did not open an honest hardware explanation");
        QTest::mouseClick(regs->viewport(), Qt::LeftButton, Qt::NoModifier, regs->visualItemRect(regs->item(0,5)).center());
        require(info->selection().kind == InformationKind::Register && info->selection().topic == "instructions" && infoFacts->text().contains("PC ="),
                "Register click did not become the newest selection");
        auto* flag = window.findChild<QLabel*>("flagZ");
        QTest::mouseClick(flag, Qt::LeftButton);
        require(info->selection().kind == InformationKind::Flag && body->toPlainText().contains("Z · zero flag"), "Flag click lost its explanation");
        emit diagram->blockActivated(SystemDiagram::Block::Ppu);
        require(info->selection().topic == "ppu" && body->toPlainText().contains("154 scanlines"), "System part did not open its hardware article");
        shot("info-2-ppu.png");
        tilesDock->raise(); QTest::qWait(20);
        auto* oam = window.findChild<QTableWidget*>("oamTable");
        QTest::mouseClick(oam->viewport(), Qt::LeftButton, Qt::NoModifier, oam->visualItemRect(oam->item(0,3)).center());
        require(info->selection().kind == InformationKind::Sprite && infoFacts->text().contains("sprite 0") &&
                tiles->selectedSprite() == 0 && game->selectedSprite() == 0 && body->toPlainText().contains("ten sprites"),
                "Sprite selection lacks linked observed facts and explanation");
        shot("info-3-sprite.png");
        auto* sheet = window.findChild<TileSheet*>("tileSheet");
        require(sheet, "Tile sheet missing");
        window.findChild<QScrollArea*>("graphicsScroll")->ensureWidgetVisible(sheet);
        QTest::mouseClick(sheet, Qt::LeftButton, Qt::NoModifier, QPoint(5,5));
        require(info->selection().kind == InformationKind::Tile && info->selection().index == tiles->selectedTile(), "Tile click did not select its explanation");
        require(tiles->selectedSprite() == -1 && body->toPlainText().contains("high × 2 + low"), "Tile explanation lacks the bit-plane decoding");
        shot("info-4-tile.png");
        window.findChild<QScrollArea*>("graphicsScroll")->verticalScrollBar()->setValue(0);
        graphicsTabs->setCurrentIndex(1); QTest::qWait(20);
        auto* tileMap = window.findChild<TileMapView*>("tileMapView");
        auto* mapChoice = window.findChild<QComboBox*>("tileMapChoice");
        require(tileMap && mapChoice, "Background/window map view missing");
        mapChoice->setCurrentIndex(3); emit mapChoice->activated(3);
        QTest::mouseClick(tileMap, Qt::LeftButton, Qt::NoModifier, tileMap->rect().center());
        require(info->selection().kind == InformationKind::MapCell && info->selection().address == tileMap->selectedAddress() &&
                tileMap->base() == 0x9C00 && tiles->selectedTile() == tileMap->selectedTile() &&
                infoFacts->text().contains("$9C00") && body->toPlainText().contains("map base + y × 32 + x"),
                "Map cell is not linked to its source byte, pattern, and general explanation");
        shot("info-5-tile-map.png");
        // Signed map addressing is resolved from copied VRAM, not from the
        // map byte as if it were always a physical pattern number.
        auto signedMap = window.displayedSnapshot().video;
        signedMap.lcdc &= ~0x10; signedMap.vram[0x1C00] = 1;
        TileMapView decodedMap; decodedMap.resize(256,256);
        decodedMap.setSnapshot(signedMap); decodedMap.setMode(3);
        QTest::mouseClick(&decodedMap, Qt::LeftButton, Qt::NoModifier, QPoint(9,9));
        require(decodedMap.selectedAddress() == 0x9C00 && decodedMap.selectedTile() == 257, "Signed map entry resolved to the wrong pattern");
        signedMap.lcdc |= 0x10; decodedMap.setSnapshot(signedMap);
        require(decodedMap.selectedTile() == 1, "Unsigned map entry was treated as a signed pattern offset");
        window.refresh(); graphicsTabs->setCurrentIndex(0);
        info->showTopic("memory");
        require(window.engine().stateBytes() == readingState, "Reading, history, or graphics selection mutated emulator state");
        body->setFocus(); QTest::keyClick(body, Qt::Key_Right); window.refresh();
        require(window.displayedSnapshot().heldButtons == 0, "Reading navigation pressed a game button");
        QTest::qWait(20); body->verticalScrollBar()->setValue(body->verticalScrollBar()->maximum()/2);
        const auto scrollPosition = body->verticalScrollBar()->value();
        window.instructionStep();
        require(info->selection().topic == "memory" && body->verticalScrollBar()->value() == scrollPosition,
                "Executing reset the topic or reading position");
        window.frameStep(); window.run(); QTest::qWait(40); window.pause();
        require(info->selection().topic == "memory", "Execution controls replaced contextual information");
        infoDock->hide(); window.findChild<QAction*>("informationMenuAction")->trigger();
        require(infoDock->isVisible() && info->selection().topic == "memory", "F1 did not restore the selected information");

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

        // Another ROM keeps the full general reference without inventing game symbols.
        QTemporaryDir dir; auto modified=demo::rom; modified[0x3000]=1;
        const auto path=dir.filePath("other.gb");
        { QFile f(path); require(f.open(QIODevice::WriteOnly) && f.write(reinterpret_cast<const char*>(modified.data()),modified.size())==qint64(modified.size()),"Fixture write failed"); }
        window.loadFile(path);
        require(!window.displayedSnapshot().teaching && info->selection().topic == "explore", "New ROM retained stale selection facts");
        const auto otherState = window.engine().stateBytes(); window.selectAddress(0xC000);
        require(info->selection().topic == "wram" && !infoFacts->text().contains("player_x") &&
                !body->toPlainText().contains("load the teaching"), "General reference invented arbitrary-ROM semantics");
        require(window.engine().stateBytes() == otherState, "General information replaced or advanced a user ROM");
        window.loadTeaching();

        // Cartridge panel for the teaching ROM: no MBC, so nothing switches.
        auto* cartridgeDock=window.findChild<QDockWidget*>("cartridgeDock");
        auto* facts=window.findChild<QLabel*>("cartridgeFacts");
        auto* explanation=window.findChild<QLabel*>("cartridgeExplanation");
        auto* switches=window.findChild<QTableWidget*>("bankSwitchTable");
        auto* bankButton=window.findChild<QPushButton*>("runUntilBankChangeButton");
        auto* windowLabel=window.findChild<QLabel*>("memoryWindowLabel");
        auto* cpuText=window.findChild<QLabel*>("instructionLabel");
        require(cartridgeDock && facts && explanation && switches && bankButton && windowLabel && window.findChild<BankMap*>("bankMap"),"Cartridge controls absent");
        emit diagram->blockActivated(SystemDiagram::Block::Cartridge); QTest::qWait(20);
        require(!cartridgeDock->visibleRegion().isEmpty(),"Diagram cartridge block did not open the Cartridge panel");
        require(facts->text().contains("ROM ONLY") && explanation->text().contains("no memory bank controller") && !bankButton->isEnabled(),"No-MBC cartridge description wrong");
        shot("tab-cartridge-teaching.png");

        // Bundled MBC1 demo remains an ordinary playable example; explicit input drives real switches.
        window.loadBankDemo();
        const auto demoStart=window.displayedSnapshot();
        require(demoStart.bankDemo && demoStart.cartridge.banks.rom==1,"Bank demo did not start ready");
        require(!cartridgeDock->visibleRegion().isEmpty() && facts->text().contains("MBC1+RAM+BATTERY") && bankButton->isEnabled(),"Bank demo cartridge panel wrong");
        require(windowLabel->isVisible() && windowLabel->text().contains("ROM bank 1") && memory->item(0,1)->text().startsWith(QString::fromStdString(hex(bankdemo::rom[0x4000],2)).mid(1)),
                "Switchable window does not show bank 1 storage");
        shot("bank-0-start.png");
        game->setFocus(); QTest::keyPress(game,Qt::Key_Z);
        window.runUntilBankChange(); QTest::keyRelease(game,Qt::Key_Z);
        auto switched=window.displayedSnapshot();
        require(switched.cartridge.banks.rom==2 && switched.next.pc==bankdemo::call_bank,"Explicit input/watch did not stop after the switch");
        require(switches->rowCount()>0 && switches->item(0,2)->text().startsWith("$2000 ← $02") && switches->item(0,3)->text()=="ROM bank 1 → 2","Bank switch list wrong");
        require(windowLabel->text().contains("ROM bank 2") && memory->item(0,1)->text().startsWith(QString::fromStdString(hex(bankdemo::rom[2*0x4000],2)).mid(1)),
                "Memory panel did not follow the bank switch");
        shot("bank-1-switched.png");
        instruction->trigger();
        require(window.displayedSnapshot().next.pc==0x4000 && window.displayedSnapshot().next.bank==2 && cpuText->text().contains("$4000 bank 2"),
                "CPU panel does not name the bank of banked code");
        // Writer evidence for an MBC write names the register and its effect.
        window.selectAddress(0x2000);
        require(writer->text().contains("command") && writer->text().contains("ROM bank 1 → 2"),"MBC writer text wrong");
        window.frameStep(); window.frameStep();
        game->setFocus(); QTest::keyPress(game,Qt::Key_Z); window.runUntilBankChange(); QTest::keyRelease(game,Qt::Key_Z);
        require(window.displayedSnapshot().cartridge.banks.rom==3,"Second press did not select bank 3");
        // The panel's own button: without a new press, nothing switches within the limit.
        auto none=window.runUntilBankChange();
        require(none.stop==WatchResult::Stop::Limit && window.displayedSnapshot().cartridge.banks.rom==3,"Run until bank change stopped without a change");
        // Battery-backed RAM reaches a .sav file.
        require(window.saveBattery(),"Battery save failed");
        { QFile sav(window.batteryPath()); require(sav.open(QIODevice::ReadOnly),"No .sav written");
          const auto data=sav.readAll(); require(data.size()==8192 && std::uint8_t(data[1])==0x42 && std::uint8_t(data[0])==2,"Saved cartridge RAM wrong"); }

        // An arbitrary MBC5 cartridge from disk with an existing save, opened by drag and drop.
        std::vector<std::uint8_t> mbc5(bankdemo::rom.begin(),bankdemo::rom.end());
        mbc5.resize(128*1024,0xFF); mbc5[0x147]=0x1B; mbc5[0x148]=0x02;
        const auto romPath=dir.filePath("mbc5 demo.gb");
        { QFile f(romPath); require(f.open(QIODevice::WriteOnly) && f.write(reinterpret_cast<const char*>(mbc5.data()),qint64(mbc5.size()))==qint64(mbc5.size()),"Fixture write failed"); }
        { QByteArray save(8192,'\0'); save[0]=7; save[1]=0x42; QFile f(dir.filePath("mbc5 demo.sav")); require(f.open(QIODevice::WriteOnly) && f.write(save)==save.size(),"Save fixture failed"); }
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(romPath)});
        // Qt delivers a drop to the widget that accepted the drag's entry.
        QDragEnterEvent enter(QPoint(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&window,&enter);
        require(enter.isAccepted(),"Window refused a ROM drag");
        QDropEvent drop(QPointF(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&window,&drop);
        auto opened=window.displayedSnapshot();
        require(opened.cartridge.info.mbc==Mbc::Mbc5 && !opened.bankDemo && opened.instructions==0,"Dropped ROM not loaded paused at power-on");
        require(window.engine().inspect(bankdemo::saved_count)==7 && window.batteryPath()==dir.filePath("mbc5 demo.sav"),"Existing .sav not loaded");
        require(!regs->item(0,5)->text().contains("Δ"),"A new game's registers were compared with the previous game");
        require(facts->text().contains("MBC5") && facts->text().contains("8 banks") && windowLabel->isVisible()==false,"MBC5 facts wrong");
        window.setMemoryBase(0x4000);
        require(windowLabel->text().contains("ROM bank 1") && windowLabel->text().contains("$004000"),"Window label lacks the file offset");
        window.findChild<QDockWidget*>("memoryDock")->raise();
        shot("tab-memory-banked.png");
        game->setFocus(); QTest::keyPress(game,Qt::Key_Z);
        bankButton->click();
        QTest::keyRelease(game,Qt::Key_Z);
        require(window.displayedSnapshot().cartridge.banks.rom==2 && switches->item(0,3)->text()=="ROM bank 1 → 2","MBC5 bank change not shown");
        frame->trigger(); frame->trigger();
        window.loadTeaching(); // switching games saves the battery first
        { QFile sav(dir.filePath("mbc5 demo.sav")); require(sav.open(QIODevice::ReadOnly) && std::uint8_t(sav.readAll()[0])==8,"Battery not saved when switching games"); }

        // OAM DMA: the copy is named as OAM's writer, and the source byte leads to its CPU writer.
        const auto dmaRom=dmaFixture(); const auto dmaPath=dir.filePath("dma.gb");
        { QFile f(dmaPath); require(f.open(QIODevice::WriteOnly) && f.write(reinterpret_cast<const char*>(dmaRom.data()),qint64(dmaRom.size()))==qint64(dmaRom.size()),"Fixture write failed"); }
        window.loadFile(dmaPath);
        auto first=window.runUntilWritten(0xFE01);
        require(first.stop==WatchResult::Stop::Dma && first.dma && first.dma->page==0xC1,"Run until written did not stop at the first DMA copy");
        auto second=window.runUntilWritten(0xFE01); // the next frame's copy, after the CPU bumped $C101
        require(second.stop==WatchResult::Stop::Dma && window.selectedAddress()==0xFE01 && window.displayedSnapshot().video.oam[1]==2,"Second DMA copy not shown");
        require(writer->text().contains("OAM DMA") && writer->text().contains("addr:c101") && writer->text().contains("160 of 160"),"DMA not named as OAM's writer");
        auto* oamSource=window.findChild<QLabel*>("oamSource");
        require(oamSource && oamSource->text().contains("DMA") && oamSource->text().contains("$C100"),"Sprites panel does not show the DMA source");
        int dmaRow=-1;
        for (int row=0;row<writes->rowCount();++row) if (writes->item(row,2)->text().startsWith("$FF46")) { dmaRow=row; break; }
        require(dmaRow>=0 && writes->item(dmaRow,3)->text().contains("starts OAM DMA from $C100"),"DMA request row lacks its effect");
        window.findChild<QDockWidget*>("memoryDock")->raise();
        shot("dma-writer.png");
        emit writer->linkActivated("addr:c101");
        require(window.selectedAddress()==0xC101 && writer->text().contains("INC [HL]"),"Source link did not lead to the CPU writer of the shadow byte");
        window.loadTeaching();

        // Tooltips: hovering asks Qt for a tooltip; every one is answered by the explanatory card.
        window.resize(1280,930); window.resetLayout(); QTest::qWait(30);
        auto hover=[&](QWidget* w, QPoint pos) {
            QCursor::setPos(w->mapToGlobal(pos));
            QHelpEvent help(QEvent::ToolTip,pos,w->mapToGlobal(pos));
            QApplication::sendEvent(w,&help);
            return tips::currentText();
        };
        // Screen grabs include the card, which is its own window (needs a real X display).
        auto screenShot=[&](const QString& name) {
            if (shots.isEmpty()) return;
            QTest::qWait(200);
            const auto image=QGuiApplication::primaryScreen()->grabWindow(0);
            if (!image.isNull()) require(image.save(QDir(shots).filePath(name)),"Screen grab could not be saved");
        };
        window.findChild<QDockWidget*>("cpuDock")->raise();
        auto pcTip=hover(regs->viewport(),regs->visualItemRect(regs->item(0,5)).center());
        require(pcTip.contains("PC · program counter") && pcTip.contains("next instruction") && tips::current(),"Register tooltip missing");
        require(tips::current()->width()<=400,"Tooltip card too wide to read comfortably");
        screenShot("tooltip-register.png");
        auto* zero=window.findChild<QLabel*>("flagZ");
        require(zero && hover(zero,zero->rect().center()).contains("Z · zero flag"),"Flag tooltip missing");
        require(hover(diagram,QPoint(int(diagram->width()*0.3),diagram->height()/2)).contains("CPU · Sharp SM83"),"Diagram part tooltip missing");
        screenShot("tooltip-diagram.png");
        window.setMemoryBase(0xC000);
        window.findChild<QDockWidget*>("memoryDock")->raise(); QTest::qWait(20);
        auto cellTip=hover(memory->viewport(),memory->visualItemRect(memory->item(0,1)).center());
        require(cellTip.contains("$C000 player_x") && cellTip.contains("WRAM · work RAM") && cellTip.contains("Value $"),"Memory byte tooltip missing");
        screenShot("tooltip-memory.png");
        QTabBar* docks=nullptr; int cartridgeTab=-1;
        for (auto* bar : window.findChildren<QTabBar*>()) for (int i=0;i<bar->count();++i) if (bar->tabText(i)=="Cartridge · banks") { docks=bar; cartridgeTab=i; }
        require(docks && hover(docks,docks->tabRect(cartridgeTab).center()).contains("16 KiB"),"Panel tab tooltip missing");
        auto* toolbar=window.findChild<QToolBar*>("executionToolbar");
        auto* runButton=toolbar->widgetForAction(run);
        require(hover(runButton,runButton->rect().center()).contains("Run / pause"),"Toolbar tooltip missing");
        auto* bankMap=window.findChild<BankMap*>("bankMap");
        window.findChild<QDockWidget*>("cartridgeDock")->raise(); QTest::qWait(20);
        require(hover(bankMap,QPoint(30,40)).contains("lower ROM window"),"Bank map window tooltip missing");
        tilesDock->raise(); QTest::qWait(20);
        auto* detail=window.findChild<TileDetail*>("tileDetail");
        require(detail,"Tile detail missing");
        bool pixelTip=false;
        for (int x=detail->width()-12;x>detail->width()/2 && !pixelTip;x-=6) pixelTip=hover(detail,QPoint(x,100)).contains("colour number");
        require(pixelTip,"Tile pixel tooltip missing");
        screenShot("tooltip-tile-pixel.png");
        tips::hide();
        require(!tips::current(),"Tooltip did not hide");

        // Sprite animation follows OAM; manually choosing a tile pins it.
        auto animated = window.displayedSnapshot();
        tiles->setSnapshot(animated); tiles->selectSprite(0);
        animated.video.oam[2] = 3; tiles->setSnapshot(animated);
        require(tiles->selectedTile() == 3, "Sprite animation left an old tile selected");
        animated.video.lcdc |= 4; animated.video.oam[2] = 5; tiles->setSnapshot(animated);
        require(tiles->selectedTile() == 4, "8x16 sprite did not select its even first tile");
        tiles->selectTile(7); animated.video.oam[2] = 9; tiles->setSnapshot(animated);
        require(tiles->selectedTile() == 7 && tiles->selectedSprite() == -1, "Manual tile did not stay pinned");
        window.loadTeaching(); require(tiles->selectedSprite() == -1 && tiles->selectedTile() == 2, "New game retained tile selection");
        const auto intervalBefore = window.displayedSnapshot().activity;
        window.setMemoryBase(0xFE00);
        require(window.displayedSnapshot().activity.instructions == intervalBefore.instructions &&
                window.displayedSnapshot().activity.startTicks == intervalBefore.startTicks, "Paused browsing erased the activity interval");
        tilesDock->raise(); emit tiles->addressActivated(0x8020); QTest::qWait(20);
        auto* memoryDock = window.findChild<QDockWidget*>("memoryDock");
        require(!memoryDock->visibleRegion().isEmpty() && window.selectedAddress() == 0x8020, "Tile link did not reveal Memory");
        mapDock->raise(); emit window.findChild<ActivityMapView*>("activityMap")->addressActivated(0xC001); QTest::qWait(20);
        require(!memoryDock->visibleRegion().isEmpty() && window.selectedAddress() == 0xC001, "Map link did not reveal Memory");

        // The rendered writer must use bank identity, including historical selection.
        const auto ramRom = bankedRamFixture(); const auto ramPath = dir.filePath("banked RAM.gb");
        { QFile f(ramPath); require(f.open(QIODevice::WriteOnly) && f.write(reinterpret_cast<const char*>(ramRom.data()), qint64(ramRom.size())) == qint64(ramRom.size()), "RAM fixture write failed"); }
        window.loadFile(ramPath); window.runUntilWritten(0xA000); window.runUntilWritten(0xA000);
        window.instructionStep(); window.instructionStep(); window.selectAddress(0xA000);
        require(window.displayedSnapshot().cartridge.banks.ram == 0 && writer->text().contains("Recorded RAM bank 0") &&
                writer->text().contains("after $11"), "Rendered writer came from another RAM bank");

        // Make the destination a directory after loading: saving fails reliably
        // without relying on OS permissions or privileged test-runner behavior.
        const auto failedPath = window.batteryPath();
        require(QDir().mkdir(failedPath), "Could not create failing save destination");
        const auto unsaved = window.engine().stateBytes();
        require(!window.saveBattery() && window.engine().batteryDirty(), "Save failure cleared dirty progress");
        // When saving fails, the user is asked; answer the modal prompt from its event loop.
        auto answer = [](const char* buttonName) {
            QTimer::singleShot(0, [buttonName] {
                auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                require(box && box->objectName() == "unsavedProgressPrompt", "Unsaved-progress prompt not shown");
                auto* button = buttonName ? box->findChild<QAbstractButton*>(buttonName) : box->button(QMessageBox::Cancel);
                require(button, "Unsaved-progress prompt button missing");
                button->click();
            });
        };
        answer(nullptr); window.loadTeaching();
        require(window.engine().stateBytes() == unsaved && !window.displayedSnapshot().teaching, "Cancelling the prompt allowed a game change");
        answer(nullptr); QCloseEvent close; QApplication::sendEvent(&window, &close);
        require(!close.isAccepted() && window.engine().stateBytes() == unsaved, "Cancelling the prompt allowed closing");
        auto* saveStatus = window.findChild<QLabel*>("batteryStatus");
        require(saveStatus && saveStatus->text().contains("Could not save"), "Save error was not persistent");
        const auto recovered = dir.filePath("recovered.sav");
        require(window.saveBatteryAs(recovered) && !window.engine().batteryDirty() && window.batteryPath() == recovered, "Saving elsewhere did not recover progress");
        { QFile f(recovered); require(f.open(QIODevice::ReadOnly) && f.readAll().size() == 32768, "Recovered save has wrong size"); }
        window.loadTeaching();
        // The user is never trapped: discarding unsaved progress lets them move on.
        window.loadFile(ramPath); // its .sav path is now a directory, so the save is protected
        window.runUntilWritten(0xA000);
        require(window.engine().batteryDirty() && !window.saveBattery(), "Discard fixture is not dirty and unsaveable");
        answer("discardProgressButton"); window.loadTeaching();
        require(window.displayedSnapshot().teaching && !window.engine().batteryDirty() && QFileInfo(failedPath).isDir(),
                "Discarding progress did not let the user open another game");

        // Rejected saves survive paused autosave and manual Save unchanged.
        const auto corruptRom = dir.filePath("corrupt.gb");
        { QFile f(corruptRom); require(f.open(QIODevice::WriteOnly) && f.write(reinterpret_cast<const char*>(mbc5.data()), qint64(mbc5.size())) == qint64(mbc5.size()), "Corrupt-save ROM fixture failed"); }
        const QByteArray malformed(8193, '\x42'); const auto corruptSave = dir.filePath("corrupt.sav");
        { QFile f(corruptSave); require(f.open(QIODevice::WriteOnly) && f.write(malformed) == malformed.size(), "Malformed save fixture failed"); }
        window.loadFile(corruptRom); window.runUntilWritten(bankdemo::saved_count);
        auto* autosave = window.findChild<QTimer*>("batteryAutosaveTimer"); require(autosave, "Autosave timer missing");
        autosave->setInterval(20); QTest::qWait(60);
        require(!window.running() && !window.saveBattery() && saveStatus->text().contains("protected"), "Rejected save was not protected");
        { QFile f(corruptSave); require(f.open(QIODevice::ReadOnly) && f.readAll() == malformed, "Rejected save was overwritten"); }
        const auto cleanSave = dir.filePath("new-progress.sav");
        require(window.saveBatteryAs(cleanSave), "Could not save new progress after rejection");
        window.runUntilWritten(bankdemo::saved_count); QTest::qWait(60);
        require(!window.running() && !window.engine().batteryDirty(), "Paused stepping was not autosaved");
        autosave->setInterval(3000); window.loadTeaching();

        // Render the reader at default/minimum sizes and its tile-map topic.
        if (argc>1) {
            window.loadTeaching(); window.resize(1280,930); window.resetLayout(); QTest::qWait(30);
            window.selectAddress(0xC000); QTest::qWait(30);
            require(window.grab().save(QString::fromLocal8Bit(argv[1])),"Screenshot could not be saved");
        }
        if (argc>2) {
            window.resize(window.minimumSize()); window.resetLayout(); QTest::qWait(30);
            require(window.grab().save(QString::fromLocal8Bit(argv[2])),"Minimum-size screenshot could not be saved");
            tilesDock->raise(); graphicsTabs->setCurrentIndex(1); QTest::qWait(30);
            auto* graphicsScroll = window.findChild<QScrollArea*>("graphicsScroll");
            require(graphicsScroll && graphicsScroll->verticalScrollBar()->maximum() > 0,
                    "Short graphics dock has no way to reveal the full map and caption");
            shot("minimum-tile-map.png");
            graphicsScroll->verticalScrollBar()->setValue(graphicsScroll->verticalScrollBar()->maximum());
            shot("minimum-tile-map-scrolled.png");
            window.loadBankDemo(); shot("minimum-cartridge.png");
        }
        std::cout << "PASS Qt " << qVersion() << " / " << qPrintable(QGuiApplication::platformName())
                  << ": native widgets, input/focus, run/pause, instruction/frame, run-until-written, synchronized inspectors, writer selection, "
                     "selection-driven information, related topics/history, inspection purity, stable reading, tile/map/OAM/activity panels, cartridge/bank panel, "
                     "manual bank switching, MBC writer text, any-ROM drop loading, battery .sav load/save, OAM DMA as writer, explanatory tooltips; UI heartbeats=" << heartbeats << "\n";
    } catch(const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    return 0;
}
