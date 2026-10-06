#include "ui/main_window.hpp"
#include "ui/game_view.hpp"
#include "teaching/annotations.hpp"
#include "teaching_rom.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>

namespace observatory {
namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QFont monospace() {
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#if defined(Q_OS_WIN)
    font.setFamily("Consolas"); // Qt's Windows fixed-font default is Courier New.
#endif
    return font;
}
QLabel* label(const QString& text = {}, QWidget* parent = nullptr) {
    auto* result = new QLabel(text, parent);
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return result;
}
QTableWidget* table(int rows, int columns, const QStringList& headings) {
    auto* t = new QTableWidget(rows, columns);
    t->setHorizontalHeaderLabels(headings);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->setFont(monospace());
    t->setShowGrid(false);
    return t;
}
void cell(QTableWidget* table, int row, int column, const QString& text, bool changed = false) {
    auto* item = table->item(row, column);
    if (!item) { item = new QTableWidgetItem; table->setItem(row, column, item); }
    item->setText(text);
    item->setForeground(changed ? QColor("#ffe19a") : QColor("#dce4ea"));
    item->setBackground(changed ? QColor("#57472c") : QColor("#17232d"));
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
}
MainWindow::MainWindow(std::size_t traceCapacity) : engine_(traceCapacity) {
    setWindowTitle("Console Observatory — DMG teaching lab");
    resize(1280, 930);
    setMinimumSize(1040, 790);
    buildUi();
    warmTeaching();
    refresh();
    connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
    timer_.setInterval(1);
    timer_.setTimerType(Qt::PreciseTimer);
    qApp->installEventFilter(this);
    game_->setFocus();
}
void MainWindow::buildUi() {
    setStyleSheet(R"(
        QMainWindow, QWidget { background: #111b24; color: #dce4ea; }
        QGroupBox { border: 1px solid #34424e; border-radius: 5px; margin-top: 12px; padding-top: 12px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; color: #81d9bc; }
        QToolBar { spacing: 7px; border-bottom: 1px solid #34424e; padding: 8px; }
        QToolButton, QPushButton { background: #253745; border: 1px solid #425866; border-radius: 4px; padding: 7px 12px; }
        QToolButton:hover, QPushButton:hover { background: #355361; }
        QToolButton:disabled { color: #657682; }
        QTableWidget { background: #17232d; border: 1px solid #34424e; selection-background-color: #335c68; }
        QHeaderView::section { background: #243441; color: #bacbd7; border: 0; padding: 4px; }
        QLineEdit, QComboBox { background: #243441; border: 1px solid #425866; border-radius: 3px; padding: 5px; }
        QSplitter::handle { background: #283944; }
        QStatusBar { color: #b5c7d2; border-top: 1px solid #34424e; }
    )");
    auto* toolbar = addToolBar("Execution");
    toolbar->setMovable(false);
    runAction_ = toolbar->addAction("Run · F5");
    runAction_->setObjectName("runAction");
    runAction_->setShortcut(Qt::Key_F5);
    connect(runAction_, &QAction::triggered, this, [this] { running_ ? pause() : run(); });
    stepAction_ = toolbar->addAction("Instruction · F10");
    stepAction_->setObjectName("instructionAction");
    stepAction_->setShortcut(Qt::Key_F10);
    connect(stepAction_, &QAction::triggered, this, [this] { instructionStep(); });
    frameAction_ = toolbar->addAction("Frame · F11");
    frameAction_->setObjectName("frameAction");
    frameAction_->setShortcut(Qt::Key_F11);
    connect(frameAction_, &QAction::triggered, this, [this] { frameStep(); });
    auto* restartAction = toolbar->addAction("Restart");
    restartAction->setObjectName("restartAction");
    connect(restartAction, &QAction::triggered, this, [this] { restart(); });
    toolbar->addSeparator();
    auto* open = toolbar->addAction("Open ROM…");
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] {
        pause();
        auto file = QFileDialog::getOpenFileName(this, "Load a 32 KiB DMG ROM", {}, "Game Boy ROM (*.gb)");
        if (!file.isEmpty()) loadFile(file);
    });
    auto* demo = toolbar->addAction("Teaching ROM");
    connect(demo, &QAction::triggered, this, [this] { pause(); engine_.loadTeaching(); warmTeaching(); previous_.reset(); selectedEvent_.reset(); setMemoryBase(0xC000); });
    trace_ = new QCheckBox("Capture writes");
    trace_->setObjectName("traceCheckbox");
    trace_->setChecked(true);
    toolbar->addWidget(trace_);
    connect(trace_, &QCheckBox::toggled, this, [this](bool on) { engine_.setTraceEnabled(on); selectedEvent_.reset(); refresh(); });
    auto* layout = toolbar->addAction("Reset layout");
    connect(layout, &QAction::triggered, this, [this] { restoreLayout(); });

    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(16, 12, 16, 10);
    auto* title = new QHBoxLayout;
    auto* name = new QLabel("Console Observatory");
    QFont titleFont = name->font(); titleFont.setPointSize(21); titleFont.setBold(true); name->setFont(titleFont);
    title->addWidget(name);
    title->addStretch();
    title->addWidget(new QLabel("DMG-B  /  SameBoy 1.0.3"));
    root->addLayout(title);
    cursor_ = label(); cursor_->setObjectName("cursorLabel"); root->addWidget(cursor_);
    outer_ = new QSplitter(Qt::Horizontal);
    root->addWidget(outer_, 1);

    auto* play = new QWidget;
    auto* left = new QVBoxLayout(play); left->setContentsMargins(0, 0, 10, 0);
    auto* gameBox = new QGroupBox("Game • completed display output");
    auto* gameLayout = new QVBoxLayout(gameBox);
    game_ = new GameView; gameLayout->addWidget(game_, 1);
    frameLabel_ = label(); frameLabel_->setObjectName("frameLabel"); gameLayout->addWidget(frameLabel_);
    gameLayout->addWidget(label("Arrows: move the star    •    F5: run / pause"));
    left->addWidget(gameBox, 1);
    auto* lessonBox = new QGroupBox("Follow a movement");
    auto* lessonLayout = new QVBoxLayout(lessonBox);
    position_ = label(); position_->setObjectName("positionLabel");
    auto font = position_->font(); font.setPointSize(15); position_->setFont(font);
    lessonLayout->addWidget(position_);
    lesson_ = label(); lesson_->setObjectName("lessonLabel"); lessonLayout->addWidget(lesson_);
    auto* follow = new QPushButton("Inspect player_x and its captured writer");
    follow->setObjectName("inspectMovementButton");
    connect(follow, &QPushButton::clicked, this, [this] { inspectMovement(); });
    lessonLayout->addWidget(follow);
    left->addWidget(lessonBox);
    outer_->addWidget(play);

    inspectors_ = new QSplitter(Qt::Vertical);
    outer_->addWidget(inspectors_);
    auto* cpu = new QGroupBox("CPU • instruction boundary");
    auto* cpuLayout = new QVBoxLayout(cpu);
    registers_ = table(1, 6, {"AF","BC","DE","HL","SP","PC"});
    registers_->setObjectName("registerTable");
    registers_->setFixedHeight(60);
    registers_->setSelectionMode(QAbstractItemView::NoSelection);
    registers_->verticalHeader()->setDefaultSectionSize(27);
    cpuLayout->addWidget(registers_);
    instruction_ = label(); instruction_->setObjectName("instructionLabel");
    instruction_->setFont(monospace());
    cpuLayout->addWidget(instruction_);
    hardware_ = label(); hardware_->setObjectName("hardwareLabel"); cpuLayout->addWidget(hardware_);
    inspectors_->addWidget(cpu);

    auto* mem = new QGroupBox("Memory • contents at the shared cursor");
    auto* memLayout = new QVBoxLayout(mem);
    auto* choices = new QHBoxLayout;
    region_ = new QComboBox;
    region_->setObjectName("memoryRegion");
    region_->addItem("WRAM · variables", 0xC000); region_->addItem("OAM · sprites", 0xFE00);
    region_->addItem("VRAM · tiles", 0x8000); region_->addItem("IO raw storage", 0xFF00);
    region_->addItem("ROM · entry", 0x0100);
    choices->addWidget(region_);
    address_ = new QLineEdit("C000"); address_->setObjectName("memoryAddress"); address_->setMaximumWidth(90);
    address_->setMaxLength(5); choices->addWidget(address_);
    choices->addWidget(new QLabel("Hex address; Enter to inspect"), 1);
    memLayout->addLayout(choices);
    connect(region_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) { setMemoryBase(std::uint16_t(region_->itemData(index).toUInt())); });
    connect(address_, &QLineEdit::returnPressed, this, [this] {
        auto text = address_->text().trimmed(); text.remove('$');
        bool ok = false; auto v = text.toUInt(&ok, 16);
        if (ok && v <= 0xFFFF) setMemoryBase(std::uint16_t(v));
        else statusBar()->showMessage("Enter a hexadecimal address from 0000 to FFFF.", 4000);
    });
    QStringList headers{"Address"}; for (int i = 0; i < 8; ++i) headers << QString::number(i, 16).toUpper();
    memory_ = table(16, 9, headers); memory_->setObjectName("memoryTable");
    memory_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    memory_->verticalHeader()->setDefaultSectionSize(24);
    memLayout->addWidget(memory_, 1);
    memLayout->addWidget(label("Gold + Δ: change since previous snapshot. IO: raw core storage; —: no storage. Select a byte to follow its retained writer."));
    connect(memory_, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column == 0) return;
        selectedAddress_ = memoryBase_ + row * 8 + column - 1;
        selectedEvent_.reset(); updateWriter();
    });
    inspectors_->addWidget(mem);

    auto* trace = new QGroupBox("Captured writes • evidence from earlier instructions");
    auto* traceLayout = new QVBoxLayout(trace);
    traceStatus_ = label(); traceStatus_->setObjectName("traceStatusLabel"); traceLayout->addWidget(traceStatus_);
    writes_ = table(0, 4, {"Tick end","Instruction","Address / name","Request → after"});
    writes_->setObjectName("writeTable");
    writes_->setSelectionBehavior(QAbstractItemView::SelectRows);
    writes_->verticalHeader()->setDefaultSectionSize(25);
    traceLayout->addWidget(writes_, 1);
    writer_ = label(); writer_->setObjectName("writerLabel"); traceLayout->addWidget(writer_);
    connect(writes_, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto* item = writes_->item(row, 0); if (!item) return;
        const auto id = item->data(Qt::UserRole).toULongLong();
        pause();
        auto found = std::find_if(snapshot_.writes.begin(),snapshot_.writes.end(),[id](const auto& e) { return e.id == id; });
        if (found != snapshot_.writes.end()) {
            const auto selected = found->address;
            if (selected < memoryBase_ || selected >= memoryBase_ + memoryWindow) setMemoryBase(selected);
            selectedAddress_ = selected;
        }
        selectedEvent_ = id; updateSelection(); updateWriter();
    });
    inspectors_->addWidget(trace);
    activity_ = label(); activity_->setObjectName("activityLabel"); root->addWidget(activity_);
    setCentralWidget(central);
    statusBar()->showMessage("Paused. Run, hold Right, then pause and inspect player_x. No historical state replay in this slice.");
    restoreLayout();
}
void MainWindow::restoreLayout() { outer_->setSizes({530, 690}); inspectors_->setSizes({195, 300, 295}); }
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
    runAction_->setText("Pause · F5"); stepAction_->setEnabled(false); frameAction_->setEnabled(false);
    timer_.start(); refresh(); game_->setFocus();
}
void MainWindow::pause() {
    running_ = false; timer_.stop(); runAction_->setText("Run · F5");
    stepAction_->setEnabled(true); frameAction_->setEnabled(true); refresh();
}
void MainWindow::tick() {
    auto target = runStartTick_ + std::uint64_t(wall_.nsecsElapsed()) * ticksPerSecond / 1000000000;
    // Discard excessive wall-clock debt after a slow machine or suspended window.
    if (target > engine_.ticks() + 2 * 140448) {
        runStartTick_ = engine_.ticks(); wall_.restart(); target = engine_.ticks() + 140448;
    }
    engine_.advanceTo(target, std::chrono::microseconds(3000));
    if (published_.elapsed() >= 33) { refresh(); published_.restart(); }
}
void MainWindow::instructionStep() {
    pause();
    auto result = engine_.stepInstruction(); refresh();
    statusBar()->showMessage(result.executedInstruction ? "Executed one opcode; all state inspectors share the new boundary." : "No opcode executed within two frame periods (CPU may be halted/stopped). Time advanced; the cursor shows the exact result.", 6000);
}
void MainWindow::frameStep() {
    pause(); auto result = engine_.stepFrame(); refresh();
    statusBar()->showMessage(result.completedFrame ? "Advanced to the next output callback, then finished its enclosing instruction. Frame and CPU timestamps are labeled separately." : "No completed output within the frame-step limit.", 6000);
}
void MainWindow::restart() {
    pause(); engine_.releaseButtons(); engine_.restart();
    if (snapshot_.teaching) warmTeaching();
    previous_.reset(); selectedEvent_.reset(); refresh();
}
void MainWindow::loadFile(const QString& path) {
    pause(); QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "Cannot open ROM", file.errorString()); return; }
    // Bound file reading before allocation; the first slice deliberately supports ROM-only.
    if (file.size() != 32768) { QMessageBox::warning(this, "Unsupported ROM", "Use a 32 KiB ROM-only monochrome Game Boy cartridge."); return; }
    auto bytes = file.readAll();
    try {
        engine_.loadRom(std::span(reinterpret_cast<const std::uint8_t*>(bytes.constData()), std::size_t(bytes.size())));
        if (std::equal(bytes.begin(), bytes.end(), reinterpret_cast<const char*>(demo::rom.data()))) warmTeaching();
        previous_.reset(); selectedEvent_.reset(); setMemoryBase(0xC000);
        statusBar()->showMessage("Loaded " + QFileInfo(path).fileName() + ". Minimal teaching boot; commercial-ROM compatibility is not validated.", 7000);
    } catch (const std::exception& error) { QMessageBox::warning(this, "Unsupported ROM", error.what()); }
}
void MainWindow::setMemoryBase(std::uint16_t base) {
    memoryBase_ = std::min<std::uint16_t>(base & 0xFFF8, 0xFF80);
    selectedAddress_ = std::max(memoryBase_, std::min<std::uint16_t>(base, memoryBase_ + memoryWindow - 1));
    address_->setText(q(hex(memoryBase_)).mid(1));
    selectedEvent_.reset(); refresh(); updateSelection();
}
void MainWindow::updateSelection() {
    if (selectedAddress_ >= memoryBase_ && selectedAddress_ < memoryBase_ + memoryWindow) {
        auto offset = selectedAddress_ - memoryBase_; memory_->setCurrentCell(offset / 8, offset % 8 + 1);
    }
}
void MainWindow::inspectMovement() {
    pause(); setMemoryBase(0xC000); selectedAddress_ = 0xC000; updateSelection(); updateWriter();
}
void MainWindow::refresh() {
    if (snapshot_.ticks) previous_ = snapshot_;
    snapshot_ = engine_.snapshot(memoryBase_);
    const auto& s = snapshot_;
    cursor_->setText(QString("%1 • t = %2 ticks @ 8,388,608 Hz • instruction #%3 • state copied after an atomic core step")
        .arg(running_ ? "LIVE sampled state" : "PAUSED exact state").arg(s.ticks).arg(s.instructions));
    const std::array<std::uint16_t, 6> values{s.registers.af,s.registers.bc,s.registers.de,s.registers.hl,s.registers.sp,s.registers.pc};
    std::array<std::uint16_t, 6> old{};
    if (previous_) old = {previous_->registers.af,previous_->registers.bc,previous_->registers.de,previous_->registers.hl,previous_->registers.sp,previous_->registers.pc};
    for (int i = 0; i < 6; ++i) {
        const bool changed = previous_ && values[i] != old[i];
        cell(registers_, 0, i, q(hex(values[i])) + (changed ? " Δ" : ""), changed);
    }
    auto flags = s.registers.af & 0xF0;
    auto next = QString("Next storage at %1: %2\n").arg(q(hex(s.next.pc)), q(disassemble(s.next)));
    if (s.lastExecuted) next += QString("Last opcode: %1  %2\n").arg(q(hex(s.lastExecuted->pc)), q(disassemble(*s.lastExecuted)));
    next += QString("Flags  Z:%1  N:%2  H:%3  C:%4").arg(bool(flags&0x80)).arg(bool(flags&0x40)).arg(bool(flags&0x20)).arg(bool(flags&0x10));
    instruction_->setText(next);
    hardware_->setText(QString("LY storage %1 · LCDC %2 · sprite 0 OAM: Y=%3, X=%4, tile=%5\nAll values above are raw storage at t=%6.")
        .arg(s.ly).arg(q(hex(s.lcdc,2))).arg(s.sprite[0]).arg(s.sprite[1]).arg(s.sprite[2]).arg(s.ticks));
    game_->setFrame(s);
    frameLabel_->setText(QString("Output #%1 • %2 • enclosing boundary t=%3\nThe display retains completed output during instruction stepping.")
        .arg(s.frames).arg(q(s.frameKind)).arg(s.frameBoundaryTicks));
    if (s.teaching) {
        position_->setText(QString("player_x  %1     player_y  %2").arg(s.playerX).arg(s.playerY));
        lesson_->setText(QString("Teaching annotation • source-defined movement\nRight → JOYP bit 0 → player_x at $C000 → OAM X at $FE01.\nCurrent storage: player_x=%1; OAM X=%2 (screen coordinate X = OAM X − 8). The next rendered frame reflects completed updates.\nRun and hold an arrow, pause, then inspect the captured writer. Instruction step exposes each write.").arg(s.playerX).arg(s.sprite[1]));
    } else {
        position_->setText("Unannotated ROM");
        lesson_->setText("Hardware values and captured write attempts are available. Game variable names are shown only for the exact bundled teaching-ROM bytes. The minimal boot is intended for teaching code.");
    }
    for (int row = 0; row < 16; ++row) {
        cell(memory_, row, 0, q(hex(memoryBase_ + row * 8)));
        for (int c = 0; c < 8; ++c) {
            auto index = row * 8 + c;
            bool changed = previous_ && previous_->memoryBase == s.memoryBase && previous_->memory[index] != s.memory[index];
            cell(memory_, row, c + 1, s.memoryAvailable[index] ? q(hex(s.memory[index], 2)).mid(1) + (changed ? " Δ" : "") : "—", changed);
            auto a = std::uint16_t(memoryBase_ + index);
            memory_->item(row,c+1)->setToolTip(q(hex(a)) + " " + q(addressName(a,s.teaching)) + "\nDMG ROM-only mapping; WRAM echo aliases canonicalized. IO values are raw storage, not synthesized CPU bus reads.");
        }
    }
    const auto rows = int(std::min<std::size_t>(s.writes.size(), 24));
    writes_->setRowCount(rows);
    for (int row = 0; row < rows; ++row) {
        const auto& e = s.writes[s.writes.size() - 1 - row];
        cell(writes_,row,0,QString::number(e.endTicks)); writes_->item(row,0)->setData(Qt::UserRole,qulonglong(e.id));
        cell(writes_,row,1,e.instruction ? q(hex(e.instruction->pc)) : "No opcode");
        cell(writes_,row,2,q(hex(e.address)) + " " + q(addressName(e.address,s.teaching)));
        cell(writes_,row,3,q(hex(e.requested,2)) + " → " + (e.valuesAvailable ? q(hex(e.after,2)) : "—"));
    }
    traceStatus_->setText(QString("%1 • %2/%3 retained • %4 evicted; earlier history incomplete • starts t=%5\nRecent 24 attempts shown below. Selecting evidence keeps current state at t=%6.")
        .arg(s.traceEnabled ? "Capture ON" : "Capture OFF").arg(s.writes.size()).arg(engine_.traceCapacity()).arg(s.evictedWrites).arg(s.oldestRetainedTick).arg(s.ticks));
    const auto& a = s.activity;
    activity_->setText(QString("%1 activity summary • interval [%2, %3] ticks • %4 opcodes • write attempts: VRAM %5 / WRAM %6 / OAM %7 / IO+HRAM %8%9")
        .arg(running_ ? "LIVE" : "Last published").arg(a.startTicks).arg(a.endTicks).arg(a.instructions)
        .arg(a.writes[1]).arg(a.writes[2]).arg(a.writes[3]).arg(a.writes[4]).arg(s.traceEnabled ? "" : " · writes unobserved while capture is off"));
    updateSelection(); updateWriter();
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
    QString heading = q(hex(selectedAddress_)) + " " + q(addressName(selectedAddress_,snapshot_.teaching));
    if (!found) {
        writer_->setText(heading + "\nNo retained write evidence. Capture may be disabled, the write may predate this window, or the hardware may use a path without this callback.");
        return;
    }
    const auto& e = *found;
    auto text = heading + QString(" • bank %1 • captured interval [%2, %3] ticks\n").arg(e.bank).arg(e.startTicks).arg(e.endTicks);
    text += e.instruction ? q(hex(e.instruction->pc)) + "  " + q(disassemble(*e.instruction)) : "Core activity without an opcode callback (for example, interrupt service)";
    text += QString("\nBefore %1 • requested %2 • after instruction %3. %4\n")
        .arg(e.valuesAvailable ? q(hex(e.before,2)) : "—",q(hex(e.requested,2)),e.valuesAvailable ? q(hex(e.after,2)) : "—", e.physicalStorage ? "Physical storage observed." : "Raw register/ROM storage; not proof of acceptance.");
    if (e.instruction) text += q(instructionNote(e.instruction->pc,snapshot_.teaching));
    if (text.endsWith('\n')) text += "Write attempts are instruction-level evidence; DMA/PPU fetches and pixel provenance are not captured.";
    writer_->setText(text);
}
bool MainWindow::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::WindowDeactivate && object == this) engine_.releaseButtons();
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto* widget = qobject_cast<QWidget*>(object);
        if (!widget || (widget != this && !isAncestorOf(widget)) || QApplication::activeModalWidget()) return QMainWindow::eventFilter(object,event);
        // Editing an address uses ordinary native text controls. Always release a
        // game key on key-up even if focus changed while it was held.
        auto* key = static_cast<QKeyEvent*>(event);
        auto button = buttonFor(key->key());
        if (button && !key->isAutoRepeat()) {
            if (event->type() == QEvent::KeyRelease || !qobject_cast<QLineEdit*>(widget)) {
                engine_.setButton(*button,event->type() == QEvent::KeyPress);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(object,event);
}
} // namespace observatory
