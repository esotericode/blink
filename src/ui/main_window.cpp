#include "ui/main_window.hpp"
#include "teaching/annotations.hpp"
#include "teaching_rom.hpp"
#include "ui/activity_map.hpp"
#include "ui/game_view.hpp"
#include "ui/style.hpp"
#include "ui/system_diagram.hpp"
#include "ui/tile_view.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace observatory {
namespace {
using style::q;
constexpr std::uint64_t frameTicks = 140448; // 70,224 dots at 4.194304 MHz, in 8,388,608 Hz ticks

QLabel* label(const QString& text = {}, const QString& name = {}) {
    auto* result = new QLabel(text);
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (!name.isEmpty()) result->setObjectName(name);
    return result;
}
QTableWidget* table(int rows, int columns, const QStringList& headings) {
    auto* t = new QTableWidget(rows, columns);
    t->setHorizontalHeaderLabels(headings);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->setFont(style::monospace());
    t->setShowGrid(false);
    return t;
}
void cell(QTableWidget* table, int row, int column, const QString& text, bool changed = false) {
    auto* item = table->item(row, column);
    if (!item) { item = new QTableWidgetItem; table->setItem(row, column, item); }
    item->setText(text);
    item->setForeground(changed ? style::changed : style::text);
    item->setBackground(changed ? style::changedBack : style::panel);
}
std::optional<Button> buttonFor(int key) {
    switch (key) {
    case Qt::Key_Right: return Button::Right;
    case Qt::Key_Left: return Button::Left;
    case Qt::Key_Up: return Button::Up;
    case Qt::Key_Down: return Button::Down;
    case Qt::Key_Z: return Button::A;
    case Qt::Key_X: return Button::B;
    case Qt::Key_Backspace: return Button::Select;
    case Qt::Key_Return: return Button::Start;
    default: return {};
    }
}
bool shown(QWidget* widget) { return widget->isVisible() && !widget->visibleRegion().isEmpty(); }
QString flagChip(const char* name, bool set) {
    return QString("<span style='background-color:%1; color:%2'>&nbsp;%3&nbsp;%4&nbsp;</span>")
        .arg(set ? "#2c6b5c" : "#243441", set ? "#e9fff6" : "#93a6b4", name, set ? "1" : "0");
}
}

MainWindow::MainWindow(std::size_t traceCapacity) : engine_(traceCapacity) {
    setWindowTitle("Console Observatory — DMG teaching lab");
    setMinimumSize(980, 680);
    resize(1280, 930);
    if (auto* s = screen()) {
        // Fit smaller laptop screens (e.g. 1366×768) instead of opening off-screen.
        const auto available = s->availableGeometry().size();
        if (available.width() < 1280 || available.height() < 930) {
            resize(std::max(980, std::min(1280, available.width() - 40)), std::max(680, std::min(930, available.height() - 60)));
        }
    }
    setStyleSheet(style::stylesheet());
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    buildActions();
    buildCentral();
    buildDocks();
    resetLayout();
    warmTeaching();
    refresh();
    setLessonStep(LessonStep::Start);
    connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
    timer_.setInterval(1);
    timer_.setTimerType(Qt::PreciseTimer);
    qApp->installEventFilter(this);
    statusBar()->showMessage("Paused. F5 runs; arrows move the star (Z/X = A/B, Enter/Backspace = Start/Select). Or follow the lesson below.");
    game_->setFocus();
}

void MainWindow::buildActions() {
    auto action = [this](const QString& text, const QString& name, const QKeySequence& key, auto slot) {
        auto* a = new QAction(text, this);
        a->setObjectName(name);
        if (!key.isEmpty()) { a->setShortcut(key); a->setShortcutContext(Qt::ApplicationShortcut); }
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    runAction_ = action("Run · F5", "runAction", Qt::Key_F5, [this] { running_ ? pause() : run(); });
    stepAction_ = action("Instruction · F10", "instructionAction", Qt::Key_F10, [this] { instructionStep(); });
    stepAction_->setToolTip("Execute exactly one more opcode (after any interrupt service or wait).");
    frameAction_ = action("Frame · F11", "frameAction", Qt::Key_F11, [this] { frameStep(); });
    frameAction_->setToolTip("Run until the PPU completes its next output.");
    untilAction_ = action("Until written · F9", "untilWrittenAction", Qt::Key_F9, [this] { runUntilWritten(selectedAddress_); });
    untilAction_->setToolTip("Run until the CPU writes the selected memory byte (up to one emulated second).");
    auto* restartAction = action("Restart", "restartAction", QKeySequence("Ctrl+R"), [this] { restart(); });
    traceAction_ = action("Capture writes", "traceAction", {}, [this](bool on) {
        engine_.setTraceEnabled(on); selectedEvent_.reset(); refresh();
    });
    traceAction_->setCheckable(true);
    traceAction_->setChecked(true);
    traceAction_->setToolTip("Record CPU write attempts (needed for writer evidence and Until written). Toggling clears old evidence.");
    spritesAction_ = action("Sprite outlines", "spriteOverlayAction", {}, [this](bool on) { game_->setShowSprites(on); });
    spritesAction_->setCheckable(true);
    spritesAction_->setToolTip("Outline sprites where OAM places them now, at the CPU cursor. The picture may be older.");
    changesAction_ = action("Changed pixels", "changesOverlayAction", {}, [this](bool on) { game_->setShowChanges(on); });
    changesAction_->setCheckable(true);
    changesAction_->setToolTip("Mark pixels that differ between the two most recent completed outputs.");
    auto* open = action("Open ROM…", "openAction", QKeySequence::Open, [this] {
        pause();
        auto file = QFileDialog::getOpenFileName(this, "Load a 32 KiB DMG ROM", {}, "Game Boy ROM (*.gb)");
        if (!file.isEmpty()) loadFile(file);
    });
    auto* teaching = action("Load teaching ROM", "teachingAction", {}, [this] { loadTeaching(); });
    auto* quit = action("Quit", "quitAction", QKeySequence::Quit, [this] { close(); });

    auto* file = menuBar()->addMenu("&File");
    file->addAction(open); file->addAction(teaching); file->addAction(restartAction);
    file->addSeparator(); file->addAction(quit);
    auto* emulation = menuBar()->addMenu("&Emulation");
    for (auto* a : {runAction_, stepAction_, frameAction_, untilAction_}) emulation->addAction(a);
    emulation->addSeparator(); emulation->addAction(traceAction_);
    emulation->addAction(action("Clear memory map", "clearMapAction", {}, [this] { engine_.clearActivityMap(); updatePanels(true); }));
    viewMenu_ = menuBar()->addMenu("&View");
    auto* help = menuBar()->addMenu("&Help");
    help->addAction(action("Lesson: follow one press of Right", "lessonMenuAction", Qt::Key_F1, [this] {
        stopLesson(); lessonDock_->show(); lessonDock_->raise();
    }));
    help->addSeparator();
    help->addAction(action("About Console Observatory", "aboutAction", {}, [this] {
        QMessageBox::about(this, "About Console Observatory", QString(
            "<h3>Console Observatory %1</h3><p>A native Game Boy (DMG) teaching lab: follow a button press through CPU "
            "instructions, memory, and the picture on screen.</p>"
            "<p>Emulation: SameBoy 1.0.3 core, unmodified (Expat/MIT).<br>Interface: Qt %2 Widgets (LGPLv3), dynamically linked.<br>"
            "Application, teaching ROM, and boot program: MIT.</p>"
            "<p>Runs offline. No browser, server, account, or commercial game is used.</p>").arg(OBSERVATORY_VERSION, qVersion()));
    }));
    help->addAction(action("Licenses and notices", "licensesAction", {}, [this] { showLicenses(); }));
    help->addAction(action("About Qt", "aboutQtAction", {}, [] { QApplication::aboutQt(); }));

    auto* toolbar = addToolBar("Execution");
    toolbar->setObjectName("executionToolbar");
    toolbar->setMovable(false);
    for (auto* a : {runAction_, stepAction_, frameAction_, untilAction_, restartAction}) toolbar->addAction(a);
    toolbar->addSeparator();
    toolbar->addAction(traceAction_);
    auto* spacer = new QWidget; spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    badge_ = new QLabel; badge_->setObjectName("stateBadge");
    toolbar->addWidget(badge_);
    cursor_ = new QLabel; cursor_->setObjectName("cursorLabel");
    cursor_->setToolTip("The shared cursor: every state panel shows the machine at this instruction boundary.\n"
                        "Ticks count 1/8,388,608 s since reset (SameBoy's unit; 2 ticks per CPU clock).");
    toolbar->addWidget(cursor_);
}

void MainWindow::buildCentral() {
    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(10, 8, 6, 6);
    game_ = new GameView;
    layout->addWidget(game_, 1);
    auto* overlays = new QHBoxLayout;
    overlays->addWidget(new QLabel("Overlays:"));
    for (auto* a : {spritesAction_, changesAction_}) {
        auto* b = new QToolButton; b->setDefaultAction(a); overlays->addWidget(b);
    }
    overlays->addStretch();
    layout->addLayout(overlays);
    frameLabel_ = label({}, "frameLabel");
    frameLabel_->setWordWrap(false);
    frameLabel_->setToolTip("The picture is the latest completed output. It changes only when the PPU completes another one,\n"
                            "so it can be older than the CPU cursor while you step instructions.");
    layout->addWidget(frameLabel_);
    setCentralWidget(central);
    connect(game_, &GameView::spriteClicked, this, [this](int index) {
        tilesDock_->show(); tilesDock_->raise();
        tiles_->selectSprite(index); game_->setSelectedSprite(index);
    });
}

QDockWidget* MainWindow::makeDock(const QString& title, const QString& name, QWidget* content) {
    auto* dock = new QDockWidget(title, this);
    dock->setObjectName(name);
    dock->setWidget(content);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    viewMenu_->addAction(dock->toggleViewAction());
    connect(dock, &QDockWidget::visibilityChanged, this, [this](bool visible) { if (visible) updatePanels(true); });
    return dock;
}

void MainWindow::buildDocks() {
    diagram_ = new SystemDiagram;
    systemDock_ = makeDock("System overview", "systemDock", diagram_);
    connect(diagram_, &SystemDiagram::blockActivated, this, [this](SystemDiagram::Block block) {
        using B = SystemDiagram::Block;
        auto memoryAt = [this](std::uint16_t a) { memoryDock_->show(); memoryDock_->raise(); selectAddress(a); };
        switch (block) {
        case B::Joypad: memoryAt(0xFF00); break;
        case B::Cartridge: memoryAt(0x0100); break;
        case B::Wram: memoryAt(0xC000); break;
        case B::Oam: case B::Vram: tilesDock_->show(); tilesDock_->raise(); break;
        case B::Ppu: case B::Lcd: memoryAt(0xFF40); break;
        case B::Cpu: cpuDock_->show(); cpuDock_->raise(); break;
        }
    });

    auto* cpu = new QWidget;
    auto* cpuLayout = new QVBoxLayout(cpu);
    cpuLayout->setContentsMargins(6, 6, 6, 6);
    registers_ = table(1, 6, {"AF", "BC", "DE", "HL", "SP", "PC"});
    registers_->setObjectName("registerTable");
    registers_->setFixedHeight(56);
    registers_->setSelectionMode(QAbstractItemView::NoSelection);
    registers_->verticalHeader()->setDefaultSectionSize(26);
    cpuLayout->addWidget(registers_);
    flags_ = label({}, "flagsLabel");
    flags_->setTextFormat(Qt::RichText);
    cpuLayout->addWidget(flags_);
    instruction_ = label({}, "instructionLabel");
    instruction_->setFont(style::monospace());
    cpuLayout->addWidget(instruction_);
    cpuLayout->addStretch();
    cpuDock_ = makeDock("CPU · instruction boundary", "cpuDock", cpu);

    auto* mem = new QWidget;
    auto* memLayout = new QVBoxLayout(mem);
    memLayout->setContentsMargins(6, 6, 6, 6);
    auto* choices = new QHBoxLayout;
    region_ = new QComboBox;
    region_->setObjectName("memoryRegion");
    region_->addItem("WRAM · variables", 0xC000); region_->addItem("OAM · sprites", 0xFE00);
    region_->addItem("VRAM · tiles", 0x8000); region_->addItem("VRAM · tile map", 0x9800);
    region_->addItem("IO registers", 0xFF00); region_->addItem("HRAM · high RAM", 0xFF80);
    region_->addItem("ROM · entry", 0x0100);
    choices->addWidget(region_);
    address_ = new QLineEdit("C000"); address_->setObjectName("memoryAddress"); address_->setMaximumWidth(80);
    address_->setMaxLength(5); address_->setToolTip("Hexadecimal address; press Enter to inspect.");
    choices->addWidget(address_);
    auto* until = new QPushButton("Run until written · F9");
    until->setObjectName("runUntilWrittenButton");
    until->setToolTip(untilAction_->toolTip());
    connect(until, &QPushButton::clicked, this, [this] { runUntilWritten(selectedAddress_); });
    choices->addStretch();
    choices->addWidget(until);
    memLayout->addLayout(choices);
    connect(region_, qOverload<int>(&QComboBox::activated), this, [this](int index) { setMemoryBase(std::uint16_t(region_->itemData(index).toUInt())); });
    connect(address_, &QLineEdit::returnPressed, this, [this] {
        auto text = address_->text().trimmed(); text.remove('$');
        bool ok = false; auto v = text.toUInt(&ok, 16);
        if (ok && v <= 0xFFFF) selectAddress(std::uint16_t(v));
        else statusBar()->showMessage("Enter a hexadecimal address from 0000 to FFFF.", 4000);
    });
    QStringList headers{"Address"}; for (int i = 0; i < 8; ++i) headers << QString::number(i, 16).toUpper();
    memory_ = table(16, 9, headers); memory_->setObjectName("memoryTable");
    memory_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    memory_->verticalHeader()->setDefaultSectionSize(22);
    memLayout->addWidget(memory_, 1);
    memory_->setToolTip("Gold + Δ: changed since the previous snapshot. IO: raw core storage, not a CPU bus read. —: no storage.\n"
                        "Click a byte to see its last captured writer; F9 runs until it is written.");
    selection_ = label({}, "selectionLabel");
    memLayout->addWidget(selection_);
    writer_ = label({}, "writerLabel");
    memLayout->addWidget(writer_);
    connect(memory_, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column == 0) return;
        selectedEvent_.reset();
        selectAddress(std::uint16_t(memoryBase_ + row * 8 + column - 1));
    });
    memoryDock_ = makeDock("Memory", "memoryDock", mem);

    tiles_ = new TileInspector;
    tilesDock_ = makeDock("Sprites and tiles", "tilesDock", tiles_);
    connect(tiles_, &TileInspector::spriteSelected, this, [this](int index) { game_->setSelectedSprite(index); });
    connect(tiles_, &TileInspector::addressActivated, this, [this](std::uint16_t a) {
        selectAddress(a);
        statusBar()->showMessage(QString("Selected %1 in the Memory panel.").arg(q(hex(a))), 4000);
    });

    map_ = new ActivityPanel;
    mapDock_ = makeDock("Memory map", "mapDock", map_);
    connect(map_, &ActivityPanel::clearRequested, this, [this] { engine_.clearActivityMap(); updatePanels(true); });
    connect(map_->view(), &ActivityMapView::addressActivated, this, [this](std::uint16_t a) {
        selectAddress(a);
        statusBar()->showMessage(QString("Selected %1 in the Memory panel.").arg(q(hex(a))), 4000);
    });

    auto* trace = new QWidget;
    auto* traceLayout = new QVBoxLayout(trace);
    traceLayout->setContentsMargins(6, 6, 6, 6);
    traceStatus_ = label({}, "traceStatusLabel");
    traceLayout->addWidget(traceStatus_);
    writes_ = table(0, 4, {"Tick end", "Instruction", "Address / name", "Before → after"});
    writes_->setObjectName("writeTable");
    writes_->setSelectionBehavior(QAbstractItemView::SelectRows);
    writes_->verticalHeader()->setDefaultSectionSize(23);
    writes_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    writes_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    traceLayout->addWidget(writes_, 1);
    connect(writes_, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto* item = writes_->item(row, 0); if (!item) return;
        const auto id = item->data(Qt::UserRole).toULongLong();
        pause();
        auto found = std::find_if(snapshot_.writes.begin(), snapshot_.writes.end(), [id](const auto& e) { return e.id == id; });
        if (found == snapshot_.writes.end()) return;
        selectAddress(found->address);
        selectedEvent_ = id; updateWriter();
    });
    writesDock_ = makeDock("Captured writes", "writesDock", trace);

    auto* lesson = new QWidget;
    auto* lessonLayout = new QVBoxLayout(lesson);
    lessonLayout->setContentsMargins(10, 6, 10, 8);
    lessonProgress_ = label({}, "lessonProgress");
    lessonProgress_->setTextFormat(Qt::RichText);
    lessonLayout->addWidget(lessonProgress_);
    lessonHeading_ = label({}, "lessonHeading");
    auto headingFont = lessonHeading_->font(); headingFont.setPointSizeF(headingFont.pointSizeF() + 3); headingFont.setBold(true);
    lessonHeading_->setFont(headingFont);
    lessonLayout->addWidget(lessonHeading_);
    lessonBody_ = label({}, "lessonBody");
    lessonBody_->setTextFormat(Qt::RichText);
    lessonBody_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto* scroll = new QScrollArea;
    scroll->setWidget(lessonBody_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    lessonLayout->addWidget(scroll, 1);
    auto* buttons = new QHBoxLayout;
    lessonAction_ = new QPushButton; lessonAction_->setObjectName("primaryAction");
    auto actionFont = lessonAction_->font(); actionFont.setBold(true); lessonAction_->setFont(actionFont);
    connect(lessonAction_, &QPushButton::clicked, this, [this] { advanceLesson(); });
    buttons->addWidget(lessonAction_);
    lessonStop_ = new QPushButton("Stop"); lessonStop_->setObjectName("lessonStop");
    lessonStop_->setToolTip("Stop the lesson and release the simulated Right press.");
    connect(lessonStop_, &QPushButton::clicked, this, [this] { stopLesson(); });
    buttons->addWidget(lessonStop_);
    buttons->addStretch();
    auto* follow = new QPushButton("Inspect player_x");
    follow->setObjectName("inspectMovementButton");
    follow->setToolTip("Select player_x ($C000) in the Memory panel and show its last captured writer.");
    connect(follow, &QPushButton::clicked, this, [this] { inspectMovement(); });
    buttons->addWidget(follow);
    lessonLayout->addLayout(buttons);
    lessonDock_ = makeDock("Lesson", "lessonDock", lesson);

    viewMenu_->addSeparator();
    viewMenu_->addAction(spritesAction_);
    viewMenu_->addAction(changesAction_);
    viewMenu_->addSeparator();
    auto* layoutAction = new QAction("Reset layout", this);
    layoutAction->setObjectName("resetLayoutAction");
    connect(layoutAction, &QAction::triggered, this, [this] { resetLayout(); });
    viewMenu_->addAction(layoutAction);

    activityLabel_ = new QLabel; activityLabel_->setObjectName("activityLabel");
    statusBar()->addPermanentWidget(activityLabel_);
}

void MainWindow::resetLayout() {
    const QList<QDockWidget*> all{systemDock_, cpuDock_, memoryDock_, tilesDock_, mapDock_, writesDock_, lessonDock_};
    for (auto* d : all) { d->setFloating(false); removeDockWidget(d); }
    addDockWidget(Qt::RightDockWidgetArea, systemDock_);
    addDockWidget(Qt::RightDockWidgetArea, cpuDock_);
    addDockWidget(Qt::RightDockWidgetArea, memoryDock_);
    tabifyDockWidget(memoryDock_, tilesDock_);
    tabifyDockWidget(tilesDock_, mapDock_);
    tabifyDockWidget(mapDock_, writesDock_);
    addDockWidget(Qt::BottomDockWidgetArea, lessonDock_);
    for (auto* d : all) d->show();
    memoryDock_->raise();
    const int h = height();
    resizeDocks({systemDock_, cpuDock_, memoryDock_}, {std::max(150, h / 5), std::max(140, h / 6), h}, Qt::Vertical);
    // The game needs about 500 px for 3× scale; the inspectors get the rest.
    resizeDocks({systemDock_}, {std::max(560, width() - 520)}, Qt::Horizontal);
    // Leave the game enough height for 3× scale at the default size, 2× at the minimum.
    resizeDocks({lessonDock_}, {std::clamp(h - 640, 220, 300)}, Qt::Vertical);
}

void MainWindow::warmTeaching() {
    // First-use convenience: stop at a completed visible frame after initialization.
    int visibleFrames = 0;
    for (int i = 0; i < 12; ++i) {
        engine_.stepFrame();
        const auto state = engine_.snapshot();
        // SameBoy can issue retained/artificial output while the LCD starts.
        // Wait for two real VBlank frames so first use includes the actual star.
        if (state.frameKind == "VBlank frame" && state.playerX == 72 && ++visibleFrames == 2) break;
    }
}
void MainWindow::run() {
    if (running_) return;
    running_ = true; runStartTick_ = engine_.ticks(); wall_.restart(); published_.restart();
    runAction_->setText("Pause · F5");
    for (auto* a : {stepAction_, frameAction_, untilAction_}) a->setEnabled(false);
    timer_.start(); refresh(); game_->setFocus();
}
void MainWindow::pause() {
    const bool was = running_;
    running_ = false; timer_.stop(); runAction_->setText("Run · F5");
    for (auto* a : {stepAction_, frameAction_, untilAction_}) a->setEnabled(true);
    if (was) refresh();
}
void MainWindow::tick() {
    auto target = runStartTick_ + std::uint64_t(wall_.nsecsElapsed()) * ticksPerSecond / 1000000000;
    // Discard excessive wall-clock debt after a slow machine or suspended window.
    if (target > engine_.ticks() + 2 * frameTicks) {
        runStartTick_ = engine_.ticks(); wall_.restart(); target = engine_.ticks() + frameTicks;
    }
    engine_.advanceTo(target, std::chrono::microseconds(3000));
    if (published_.elapsed() >= 33) { refresh(); published_.restart(); }
}
void MainWindow::instructionStep() {
    pause();
    auto result = engine_.stepInstruction(); refresh();
    statusBar()->showMessage(result.executedInstruction ? "Executed one opcode; every state panel shows the new boundary."
        : "No opcode executed within two frame periods (CPU may be halted/stopped). Time advanced; the cursor shows the exact result.", 6000);
}
void MainWindow::frameStep() {
    pause(); auto result = engine_.stepFrame(); refresh();
    statusBar()->showMessage(result.completedFrame ? "Advanced to the next completed output, then finished its enclosing instruction."
        : "No completed output within the frame-step limit.", 6000);
}
WatchResult MainWindow::runUntilWritten(std::uint16_t address) {
    pause();
    selectedEvent_.reset();
    auto result = engine_.runUntilWrite(address, 60 * frameTicks);
    refresh();
    selectAddress(address);
    const auto where = q(hex(address)) + (addressName(address, snapshot_.teaching).empty() ? QString() : " " + q(addressName(address, snapshot_.teaching)));
    if (result.stop == WatchResult::Stop::CaptureOff) {
        statusBar()->showMessage("Turn on Capture writes to stop on a write.", 6000);
    } else if (result.stop == WatchResult::Stop::Write) {
        const auto& w = *result.write;
        statusBar()->showMessage(QString("Stopped after the write to %1 by %2 (%3 instructions, %4 frames later).")
            .arg(where, w.instruction ? q(hex(w.instruction->pc)) + " " + q(disassemble(*w.instruction)) : QString("interrupt/wait work"))
            .arg(result.instructions).arg(result.frames), 10000);
    } else {
        statusBar()->showMessage(QString("No write to %1 within 60 frames (one emulated second); paused at the limit. "
            "Some writes need input, e.g. hold an arrow key.").arg(where), 10000);
    }
    return result;
}
void MainWindow::restart() {
    pause(); engine_.releaseButtons(); engine_.restart();
    if (snapshot_.teaching) warmTeaching();
    afterLoad();
}
void MainWindow::loadTeaching() {
    pause(); engine_.loadTeaching(); warmTeaching(); afterLoad(); setMemoryBase(0xC000);
}
void MainWindow::afterLoad() {
    previous_.reset(); selectedEvent_.reset();
    // Overlays compare across time; a new session starts without them.
    spritesAction_->setChecked(false); changesAction_->setChecked(false);
    game_->setShowSprites(false); game_->setShowChanges(false); game_->setSelectedSprite(-1);
    refresh();
    setLessonStep(snapshot_.teaching ? LessonStep::Start : LessonStep::NeedsTeachingRom);
}
void MainWindow::loadFile(const QString& path) {
    pause(); QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "Cannot open ROM", file.errorString()); return; }
    // Bound file reading before allocation; this slice deliberately supports ROM-only.
    if (file.size() != 32768) { QMessageBox::warning(this, "Unsupported ROM", "Use a 32 KiB ROM-only monochrome Game Boy cartridge."); return; }
    auto bytes = file.readAll();
    try {
        engine_.loadRom(std::span(reinterpret_cast<const std::uint8_t*>(bytes.constData()), std::size_t(bytes.size())));
        if (std::equal(bytes.begin(), bytes.end(), reinterpret_cast<const char*>(demo::rom.data()))) warmTeaching();
        afterLoad(); setMemoryBase(0xC000);
        statusBar()->showMessage("Loaded " + QFileInfo(path).fileName() + ". Minimal teaching boot; commercial-ROM compatibility is not validated.", 7000);
    } catch (const std::exception& error) { QMessageBox::warning(this, "Unsupported ROM", error.what()); }
}
void MainWindow::setMemoryBase(std::uint16_t base) {
    memoryBase_ = std::min<std::uint16_t>(base & 0xFFF8, 0xFF80);
    selectedAddress_ = std::max(memoryBase_, std::min<std::uint16_t>(base, memoryBase_ + memoryWindow - 1));
    address_->setText(q(hex(memoryBase_)).mid(1));
    // Show which region the window is in; blank when it is none of the listed ones.
    struct Range { std::uint16_t first, last; };
    static const Range ranges[] = {{0xC000, 0xDFFF}, {0xFE00, 0xFE9F}, {0x8000, 0x97FF}, {0x9800, 0x9FFF}, {0xFF00, 0xFF7F}, {0xFF80, 0xFFFF}, {0x0000, 0x7FFF}};
    int index = -1;
    for (int i = 0; i < int(std::size(ranges)); ++i) if (memoryBase_ >= ranges[i].first && memoryBase_ <= ranges[i].last) { index = i; break; }
    region_->setCurrentIndex(index);
    selectedEvent_.reset(); refresh();
}
void MainWindow::selectAddress(std::uint16_t address) {
    if (address < memoryBase_ || address >= memoryBase_ + memoryWindow) setMemoryBase(address);
    selectedAddress_ = address;
    map_->view()->setSelectedAddress(address);
    updateSelection(); updateWriter();
}
void MainWindow::updateSelection() {
    if (selectedAddress_ >= memoryBase_ && selectedAddress_ < memoryBase_ + memoryWindow) {
        auto offset = selectedAddress_ - memoryBase_;
        memory_->setCurrentCell(offset / 8, offset % 8 + 1);
        const auto value = snapshot_.memory[offset];
        const auto name = addressName(selectedAddress_, snapshot_.teaching);
        selection_->setText(QString("<b>%1</b>%2 · %3 · value %4")
            .arg(q(hex(selectedAddress_)), name.empty() ? QString() : " " + q(name), q(regionName(selectedAddress_)),
                 snapshot_.memoryAvailable[offset] ? QString("%1 (%2)").arg(q(hex(value, 2))).arg(value) : QString("— (no storage)")));
    }
}
void MainWindow::inspectMovement() {
    pause(); memoryDock_->show(); memoryDock_->raise(); setMemoryBase(0xC000); selectAddress(0xC000);
}

void MainWindow::setLessonStep(LessonStep step, const LessonEvidence& evidence, const QString& problem) {
    lessonStep_ = step;
    const auto page = lessonPage(step, snapshot_, evidence);
    lessonHeading_->setText(q(page.heading));
    lessonBody_->setText(q(page.body) + (problem.isEmpty() ? QString() : "<p style='color:#f2a65a'>" + problem + "</p>"));
    lessonAction_->setText(q(page.action));
    lessonStop_->setVisible(step != LessonStep::Start && step != LessonStep::NeedsTeachingRom && step != LessonStep::Done);
    const int current = lessonStepNumber(step);
    static const char* names[] = {"Joypad", "WRAM", "OAM", "Frame", "Tile"};
    QStringList parts;
    for (int i = 1; i <= lessonStepCount; ++i) {
        const auto colour = i < current ? style::accent : i == current ? style::highlight : style::muted;
        parts << QString("<span style='color:%1'>%2 %3 %4</span>").arg(colour.name(), i < current ? "✓" : i == current ? "●" : "○").arg(i).arg(names[i - 1]);
    }
    lessonProgress_->setText(parts.join("&nbsp;&nbsp;→&nbsp;&nbsp;"));
    using B = SystemDiagram::Block; using P = SystemDiagram::Path;
    switch (step) {
    case LessonStep::Holding: diagram_->setHighlight({P::JoypadCpu}, {B::Joypad, B::Cpu}); break;
    case LessonStep::Stored: diagram_->setHighlight({P::CpuWram}, {B::Cpu, B::Wram}); break;
    case LessonStep::Copied: diagram_->setHighlight({P::CpuOam}, {B::Cpu, B::Oam}); break;
    case LessonStep::Drawn: diagram_->setHighlight({P::OamPpu, P::VramPpu, P::PpuLcd}, {B::Oam, B::Vram, B::Ppu, B::Lcd}); break;
    case LessonStep::Tile: diagram_->setHighlight({P::VramPpu}, {B::Vram}); break;
    default: diagram_->setHighlight({}, {}); break;
    }
}
void MainWindow::stopLesson() {
    engine_.setButton(Button::Right, false);
    spritesAction_->setChecked(false); changesAction_->setChecked(false);
    game_->setShowSprites(false); game_->setShowChanges(false); game_->setSelectedSprite(-1);
    refresh();
    setLessonStep(snapshot_.teaching ? LessonStep::Start : LessonStep::NeedsTeachingRom);
}
void MainWindow::advanceLesson() {
    pause();
    statusBar()->clearMessage();
    auto evidence = [](const WatchResult& r) { return LessonEvidence{r.write, r.instructions, r.frames, 0}; };
    auto needCapture = [this] {
        if (traceAction_->isChecked()) return;
        traceAction_->setChecked(true); engine_.setTraceEnabled(true);
        statusBar()->showMessage("Capture writes turned on: the lesson stops on real write attempts.", 6000);
    };
    switch (lessonStep_) {
    case LessonStep::NeedsTeachingRom:
        loadTeaching();
        return;
    case LessonStep::Start:
    case LessonStep::Done:
        if (!snapshot_.teaching) { setLessonStep(LessonStep::NeedsTeachingRom); return; }
        if (snapshot_.playerX >= 152) { restart(); statusBar()->showMessage("The star was at the right edge, so the lesson restarted the game first.", 6000); }
        engine_.setButton(Button::Right, true);
        refresh();
        setLessonStep(LessonStep::Holding);
        return;
    case LessonStep::Holding: {
        needCapture();
        engine_.setButton(Button::Right, true);
        const auto r = engine_.runUntilWrite(demo::player_x, 4 * frameTicks);
        refresh();
        if (r.stop != WatchResult::Stop::Write) {
            setLessonStep(LessonStep::Holding, {}, "No write to player_x happened within four frames. If the star is at the right edge "
                                                   "(player_x = 152) the game skips the store; use Restart, then try again.");
            return;
        }
        memoryDock_->show(); memoryDock_->raise();
        selectAddress(demo::player_x);
        setLessonStep(LessonStep::Stored, evidence(r));
        return;
    }
    case LessonStep::Stored: {
        needCapture();
        const auto r = engine_.runUntilWrite(0xFE01, 2 * frameTicks);
        refresh();
        if (r.stop != WatchResult::Stop::Write) { setLessonStep(LessonStep::Stored, {}, "No write to OAM X happened within two frames."); return; }
        spritesAction_->setChecked(true); game_->setShowSprites(true); game_->setSelectedSprite(0);
        selectAddress(0xFE01);
        setLessonStep(LessonStep::Copied, evidence(r));
        return;
    }
    case LessonStep::Copied: {
        const auto r = engine_.stepFrame();
        refresh();
        changesAction_->setChecked(true); game_->setShowChanges(true);
        setLessonStep(LessonStep::Drawn, LessonEvidence{{}, 0, r.completedFrame ? 1u : 0u, game_->changedPixels()});
        return;
    }
    case LessonStep::Drawn:
        tilesDock_->show(); tilesDock_->raise();
        tiles_->selectSprite(0); game_->setSelectedSprite(0);
        setLessonStep(LessonStep::Tile);
        return;
    case LessonStep::Tile:
        engine_.setButton(Button::Right, false);
        refresh();
        setLessonStep(LessonStep::Done);
        return;
    }
}

void MainWindow::refresh() {
    if (snapshot_.ticks) previous_ = snapshot_;
    snapshot_ = engine_.snapshot(memoryBase_);
    const auto& s = snapshot_;
    badge_->setText(running_ ? "RUNNING" : "PAUSED");
    badge_->setStyleSheet(running_ ? "background:#2c6b5c; color:#e9fff6;" : "background:#57472c; color:#ffe19a;");
    cursor_->setText(QString("t = %1 ticks\ninstruction #%2 · output #%3").arg(s.ticks).arg(s.instructions).arg(s.frames));
    const std::array<std::uint16_t, 6> values{s.registers.af, s.registers.bc, s.registers.de, s.registers.hl, s.registers.sp, s.registers.pc};
    std::array<std::uint16_t, 6> old{};
    if (previous_) old = {previous_->registers.af, previous_->registers.bc, previous_->registers.de, previous_->registers.hl, previous_->registers.sp, previous_->registers.pc};
    for (int i = 0; i < 6; ++i) {
        const bool changed = previous_ && values[i] != old[i];
        cell(registers_, 0, i, q(hex(values[i])) + (changed ? " Δ" : ""), changed);
    }
    const auto f = s.registers.af;
    flags_->setText("Flags " + flagChip("Z", f & 0x80) + " " + flagChip("N", f & 0x40) + " " + flagChip("H", f & 0x20) + " " + flagChip("C", f & 0x10) +
                    QString("&nbsp;&nbsp; A = %1 (%2)").arg(q(hex(f >> 8, 2))).arg(f >> 8));
    auto text = QString("Next  %1  %2   ← storage at PC, not yet executed\n").arg(q(hex(s.next.pc)), q(disassemble(s.next)));
    if (s.lastExecuted) text += QString("Last  %1  %2\n").arg(q(hex(s.lastExecuted->pc)), q(disassemble(*s.lastExecuted)));
    const auto note = s.lastExecuted ? instructionNote(s.lastExecuted->pc, s.teaching) : std::string();
    if (!note.empty()) text += q(note);
    instruction_->setText(text.trimmed());
    game_->setFrame(s);
    frameLabel_->setText(QString("Picture: output #%1 · %2 · completed at t=%3")
        .arg(s.frames).arg(q(s.frameKind)).arg(s.frameBoundaryTicks));
    for (int row = 0; row < 16; ++row) {
        cell(memory_, row, 0, q(hex(memoryBase_ + row * 8)));
        for (int c = 0; c < 8; ++c) {
            auto index = row * 8 + c;
            bool changed = previous_ && previous_->memoryBase == s.memoryBase && previous_->memory[index] != s.memory[index];
            cell(memory_, row, c + 1, s.memoryAvailable[index] ? q(hex(s.memory[index], 2)).mid(1) + (changed ? " Δ" : "") : "—", changed);
            auto a = std::uint16_t(memoryBase_ + index);
            memory_->item(row, c + 1)->setToolTip(q(hex(a)) + " " + q(addressName(a, s.teaching)) + "\n" + q(regionName(a)) +
                "\nWRAM echo aliases are canonicalized. IO values are raw storage, not synthesized CPU bus reads.");
        }
    }
    const auto& a = s.activity;
    activityLabel_->setText(QString("%1 interval: %2 opcodes · writes VRAM %3 / WRAM %4 / OAM %5 / IO %6%7")
        .arg(running_ ? "Live" : "Last").arg(a.instructions).arg(a.writes[1]).arg(a.writes[2]).arg(a.writes[3]).arg(a.writes[4])
        .arg(s.traceEnabled ? "" : " (capture off)"));
    diagram_->setSnapshot(s);
    updatePanels(!running_);
    updateSelection(); updateWriter();
}
void MainWindow::updatePanels(bool force) {
    const auto& s = snapshot_;
    if (force || shown(writesDock_)) {
        const auto rows = int(std::min<std::size_t>(s.writes.size(), 64));
        writes_->setRowCount(rows);
        for (int row = 0; row < rows; ++row) {
            const auto& e = s.writes[s.writes.size() - 1 - row];
            cell(writes_, row, 0, QString::number(e.endTicks)); writes_->item(row, 0)->setData(Qt::UserRole, qulonglong(e.id));
            cell(writes_, row, 1, e.instruction ? q(hex(e.instruction->pc)) + "  " + q(disassemble(*e.instruction)) : "no opcode (interrupt/wait)");
            cell(writes_, row, 2, q(hex(e.address)) + " " + q(addressName(e.address, s.teaching)));
            cell(writes_, row, 3, (e.valuesAvailable ? q(hex(e.before, 2)) : "—") + " → " + (e.valuesAvailable ? q(hex(e.after, 2)) : "—"));
        }
        traceStatus_->setText(QString("%1 · %2/%3 retained · %4 evicted (earlier history incomplete) · from t=%5. "
                                      "Newest 64 shown. Selecting one keeps the current state at t=%6; it is evidence, not a replay.")
            .arg(s.traceEnabled ? "Capture ON" : "Capture OFF").arg(s.writes.size()).arg(engine_.traceCapacity())
            .arg(s.evictedWrites).arg(s.oldestRetainedTick).arg(s.ticks));
    }
    if (force || shown(tilesDock_)) tiles_->setSnapshot(s);
    if (force || shown(mapDock_)) { engine_.activityMap(activity_); map_->setMap(activity_, s.teaching); }
}
void MainWindow::updateWriter() {
    const WriteEvent* found = nullptr;
    if (selectedEvent_) {
        for (const auto& e : snapshot_.writes) if (e.id == *selectedEvent_) found = &e;
    } else {
        for (auto i = snapshot_.writes.rbegin(); i != snapshot_.writes.rend(); ++i) {
            if (i->canonicalAddress == canonicalAddress(selectedAddress_)) { found = &*i; break; }
        }
    }
    if (!found) {
        writer_->setText("Last writer: no retained evidence. Capture may be off, the write may predate the window, or the hardware "
                         "used a path without this CPU callback (e.g. DMA).");
        return;
    }
    const auto& e = *found;
    auto text = QString("Last writer (interval [%1, %2] ticks): ").arg(e.startTicks).arg(e.endTicks);
    text += e.instruction ? q(hex(e.instruction->pc)) + "  " + q(disassemble(*e.instruction)) : "core work without an opcode (e.g. interrupt service)";
    text += QString("\nBefore %1 · requested %2 · after %3. %4")
        .arg(e.valuesAvailable ? q(hex(e.before, 2)) : "—", q(hex(e.requested, 2)), e.valuesAvailable ? q(hex(e.after, 2)) : "—",
             e.physicalStorage ? "Physical storage observed." : "Raw register/ROM storage; not proof of acceptance.");
    if (e.instruction) {
        const auto note = instructionNote(e.instruction->pc, snapshot_.teaching);
        if (!note.empty()) text += "\n" + q(note);
    }
    writer_->setText(text);
}
void MainWindow::showLicenses() {
    const auto app = QCoreApplication::applicationDirPath();
    QString folder;
    for (const auto& candidate : {app + "/licenses", app + "/../share/console-observatory/licenses"}) {
        if (QDir(candidate).exists()) { folder = QDir::cleanPath(candidate); break; }
    }
    QMessageBox box(this);
    box.setWindowTitle("Licenses and notices");
    box.setTextFormat(Qt::RichText);
    box.setText("<p>Console Observatory, its teaching ROM, and boot program: MIT.<br>SameBoy 1.0.3 core: Expat/MIT.<br>"
                "Qt " + QString(qVersion()) + ": LGPLv3, dynamically linked; you may replace the Qt libraries with compatible builds.</p>"
                "<p>Full texts and third-party notices: " + (folder.isEmpty() ? QString("see <code>licenses/</code> and THIRD_PARTY_NOTICES.md in the source tree.")
                                                                               : "<code>" + folder.toHtmlEscaped() + "</code>") + "</p>");
    QPushButton* openFolder = folder.isEmpty() ? nullptr : box.addButton("Open folder", QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (openFolder && box.clickedButton() == openFolder) QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}
void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    // Dock sizes are only meaningful once the window has its real geometry.
    if (!laidOut_) { laidOut_ = true; resetLayout(); }
}
bool MainWindow::ownsKeyboard(QWidget* widget) const {
    if (!widget || QApplication::activeModalWidget()) return false;
    auto* window = widget->window();
    if (window == this) return true;
    // Floating docks are separate windows that still belong to this main window.
    auto* dock = qobject_cast<QDockWidget*>(window);
    return dock && dock->parentWidget() == this;
}
bool MainWindow::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::WindowDeactivate && object == this) engine_.releaseButtons();
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto* widget = qobject_cast<QWidget*>(object);
        if (!ownsKeyboard(widget)) return QMainWindow::eventFilter(object, event);
        auto* key = static_cast<QKeyEvent*>(event);
        auto button = buttonFor(key->key());
        // Text fields keep their keys, and Enter still activates a focused button.
        // Always release a game key on key-up even if focus changed while it was held.
        const bool textField = qobject_cast<QLineEdit*>(widget);
        const bool enterOnButton = key->key() == Qt::Key_Return && qobject_cast<QAbstractButton*>(widget);
        if (button && !key->isAutoRepeat() && (event->type() == QEvent::KeyRelease || (!textField && !enterOnButton))) {
            engine_.setButton(*button, event->type() == QEvent::KeyPress);
            return !enterOnButton;
        }
    }
    return QMainWindow::eventFilter(object, event);
}
} // namespace observatory
