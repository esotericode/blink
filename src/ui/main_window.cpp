#include "ui/main_window.hpp"
#include "teaching/annotations.hpp"
#include "teaching_rom.hpp"
#include "ui/activity_map.hpp"
#include "ui/cartridge_view.hpp"
#include "ui/game_view.hpp"
#include "ui/information_panel.hpp"
#include "teaching/information.hpp"
#include "ui/style.hpp"
#include "ui/system_diagram.hpp"
#include "ui/tile_view.hpp"
#include "ui/tooltip.hpp"
#include "teaching/glossary.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
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
#include <QMimeData>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextBrowser>
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
QString romFilter() { return "Game Boy ROMs (*.gb *.gbc *.sgb *.bin);;All files (*)"; }
std::optional<QString> droppedFile(const QMimeData* mime) {
    if (!mime || !mime->hasUrls() || mime->urls().size() != 1 || !mime->urls().front().isLocalFile()) return {};
    return mime->urls().front().toLocalFile();
}
// Tooltip for one memory byte: address and name, its region, a register's job, its value.
QString byteTip(std::uint16_t a, const QString& label, std::uint8_t value, bool available, bool changed) {
    const auto region = regionExplanation(a);
    const auto io = ioRegisterExplanation(a);
    QString body = q(io.empty() ? region.body : io.body);
    if (!io.empty()) body += "\n\n" + q(region.title) + ".";
    QString hint = available ? QString("Value %1 = %2 in decimal%3. Click to select; F9 runs until it is written.")
                                   .arg(q(hex(value, 2))).arg(value).arg(changed ? ", changed since the last snapshot (gold)" : "")
                             : QString("No memory here, so there is no value to show.");
    return tips::make(label + " · " + q(io.empty() ? region.title : io.title), body, hint);
}
QString flagChip(const char* name, bool set) {
    return QString("<span style='background-color:%1; color:%2'>&nbsp;%3&nbsp;%4&nbsp;</span>")
        .arg(set ? "#2c6b5c" : "#243441", set ? "#e9fff6" : "#93a6b4", name, set ? "1" : "0");
}
}

MainWindow::MainWindow(std::size_t traceCapacity) : engine_(traceCapacity) {
    setWindowTitle("Console Observatory — DMG exploration lab");
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
    tips::install();
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
    setAcceptDrops(true);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    buildActions();
    buildCentral();
    buildDocks();
    resetLayout();
    warmTeaching();
    romName_ = "teaching game";
    refresh();
    batteryTimer_.setParent(this); batteryTimer_.setObjectName("batteryAutosaveTimer");
    batteryTimer_.setInterval(3000);
    connect(&batteryTimer_, &QTimer::timeout, this, [this] {
        if (engine_.batteryDirty() && !batteryBlocked_) saveBattery();
    });
    batteryTimer_.start();
    connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
    timer_.setInterval(1);
    timer_.setTimerType(Qt::PreciseTimer);
    qApp->installEventFilter(this);
    statusBar()->showMessage("Paused. F5 runs; arrows move the star (Z/X = A/B, Enter/Backspace = Start/Select). "
                             "Click an item for detailed information below, or open a Game Boy ROM (Ctrl+O or drop a file).");
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
    runAction_->setToolTip(tips::key("action.run"));
    stepAction_ = action("Instruction · F10", "instructionAction", Qt::Key_F10, [this] { instructionStep(); });
    stepAction_->setToolTip(tips::key("action.step"));
    frameAction_ = action("Frame · F11", "frameAction", Qt::Key_F11, [this] { frameStep(); });
    frameAction_->setToolTip(tips::key("action.frame"));
    untilAction_ = action("Until written · F9", "untilWrittenAction", Qt::Key_F9, [this] { runUntilWritten(selectedAddress_); });
    untilAction_->setToolTip(tips::key("action.until"));
    auto* restartAction = action("Restart", "restartAction", QKeySequence("Ctrl+R"), [this] { restart(); });
    restartAction->setToolTip(tips::key("action.restart"));
    traceAction_ = action("Capture writes", "traceAction", {}, [this](bool on) {
       
        engine_.setTraceEnabled(on); selectedEvent_.reset(); refresh();
    });
    traceAction_->setCheckable(true);
    traceAction_->setChecked(true);
    traceAction_->setToolTip(tips::key("action.capture"));
    spritesAction_ = action("Sprite outlines", "spriteOverlayAction", {}, [this](bool on) { game_->setShowSprites(on); });
    spritesAction_->setCheckable(true);
    spritesAction_->setToolTip(tips::key("action.sprites"));
    changesAction_ = action("Changed pixels", "changesOverlayAction", {}, [this](bool on) { game_->setShowChanges(on); });
    changesAction_->setCheckable(true);
    changesAction_->setToolTip(tips::key("action.changes"));
    auto* open = action("Open ROM…", "openAction", QKeySequence::Open, [this] {
        pause();
        auto file = QFileDialog::getOpenFileName(this, "Open a Game Boy ROM", {}, romFilter());
        if (!file.isEmpty()) loadFile(file);
    });
    open->setToolTip(tips::key("action.open"));
    auto* teaching = action("Star example (arrow-key input)", "teachingAction", {}, [this] { loadTeaching(); });
    auto* bankDemo = action("Bank-switching demo (MBC1)", "bankDemoAction", {}, [this] { loadBankDemo(); });
    teaching->setToolTip(tips::key("action.teaching"));
    bankDemo->setToolTip(tips::key("action.bankdemo"));
    saveBatteryAction_ = action("Save battery RAM now", "saveBatteryAction", QKeySequence("Ctrl+S"), [this] {
        if (saveBattery()) statusBar()->showMessage("Saved battery-backed cartridge RAM to " + QDir::toNativeSeparators(savePath_), 6000);
    });
    saveBatteryAction_->setToolTip(tips::key("action.savebattery"));
    saveBatteryAsAction_ = action("Save battery RAM as…", "saveBatteryAsAction", {}, [this] {
        const auto path = QFileDialog::getSaveFileName(this, "Save battery RAM elsewhere", savePath_, "Battery saves (*.sav);;All files (*)");
        if (!path.isEmpty()) saveBatteryAs(path);
    });
    saveBatteryAsAction_->setToolTip(tips::key("action.savebatteryas"));
    auto* quit = action("Quit", "quitAction", QKeySequence::Quit, [this] { close(); });

    auto* file = menuBar()->addMenu("&File");
    file->addAction(open);
    auto* examples = file->addMenu("Bundled examples");
    examples->setObjectName("examplesMenu");
    examples->addAction(teaching); examples->addAction(bankDemo);
    file->addAction(saveBatteryAction_);
    file->addAction(saveBatteryAsAction_);
    file->addAction(restartAction);
    file->addSeparator(); file->addAction(quit);
    auto* emulation = menuBar()->addMenu("&Emulation");
    for (auto* a : {runAction_, stepAction_, frameAction_, untilAction_}) emulation->addAction(a);
    emulation->addSeparator(); emulation->addAction(traceAction_);
    emulation->addAction(action("Clear memory map", "clearMapAction", {}, [this] { engine_.clearActivityMap(); updatePanels(true); }));
    emulation->actions().back()->setToolTip(tips::key("action.clearmap"));
    viewMenu_ = menuBar()->addMenu("&View");
    auto* help = menuBar()->addMenu("&Help");
    help->addAction(action("Selection info", "informationMenuAction", Qt::Key_F1, [this] {
        informationDock_->show(); informationDock_->raise();
    }));
    help->addSeparator();
    help->addAction(action("About Console Observatory", "aboutAction", {}, [this] {
        QMessageBox::about(this, "About Console Observatory", QString(
            "<h3>Console Observatory %1</h3><p>A native Game Boy (DMG) exploration lab: select CPU state, memory, "
            "graphics, or cartridge hardware to inspect real values and read detailed explanations.</p>"
            "<p>Emulation: SameBoy 1.0.3 core, unmodified (Expat/MIT).<br>Interface: Qt %2 Widgets (LGPLv3), dynamically linked.<br>"
            "Application, bundled example ROMs, and boot program: MIT.</p>"
            "<p>Runs offline. Two original examples are bundled; you can also open your own ROM files.</p>").arg(OBSERVATORY_VERSION, qVersion()));
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
    badge_->setToolTip(tips::key("ui.badge"));
    toolbar->addWidget(badge_);
    cursor_ = new QLabel; cursor_->setObjectName("cursorLabel");
    cursor_->setToolTip(tips::key("ui.cursor"));
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
    frameLabel_->setToolTip(tips::key("ui.frame"));
    frameLabel_->setProperty("informationTopic", "display");
    layout->addWidget(frameLabel_);
    setCentralWidget(central);
    connect(game_, &GameView::spriteClicked, this, [this](int index) {
        tilesDock_->show(); tilesDock_->raise();
        tiles_->selectSprite(index); game_->setSelectedSprite(index);
        information_->select({InformationKind::Sprite, "oam", {}, 0, index});
        informationDock_->show();
    });
    connect(game_, &GameView::displayClicked, this, [this] { showInformation("display"); });
}

QDockWidget* MainWindow::makeDock(const QString& title, const QString& name, QWidget* content) {
    auto* dock = new QDockWidget(title, this);
    dock->setObjectName(name);
    // Explains the panel from its title bar and its tab ("systemDock" -> "panel.system").
    auto key = name; key.chop(4);
    dock->setProperty("panelTip", tips::key("panel." + key.toStdString()));
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
        case B::Cartridge: cartridgeDock_->show(); cartridgeDock_->raise(); break;
        case B::Wram: memoryAt(0xC000); break;
        case B::Oam: case B::Vram: tilesDock_->show(); tilesDock_->raise(); break;
        case B::Ppu: case B::Lcd: memoryAt(0xFF40); break;
        case B::Cpu: cpuDock_->show(); cpuDock_->raise(); break;
        }
        const char* topics[] = {"joypad", "cartridge", "cpu", "wram", "vram", "oam", "ppu", "display"};
        diagram_->setHighlight({}, {block});
        showInformation(topics[int(block)]);
    });
    connect(diagram_, &SystemDiagram::topicActivated, this, [this](const QString& topic) { showInformation(topic.toStdString()); });

    auto* cpu = new QWidget;
    auto* cpuLayout = new QVBoxLayout(cpu);
    cpuLayout->setContentsMargins(6, 6, 6, 6);
    registers_ = table(1, 6, {"AF", "BC", "DE", "HL", "SP", "PC"});
    registers_->setObjectName("registerTable");
    registers_->setFixedHeight(56);
    registers_->setSelectionMode(QAbstractItemView::NoSelection);
    registers_->verticalHeader()->setDefaultSectionSize(26);
    {
        const char* keys[] = {"reg.af", "reg.bc", "reg.de", "reg.hl", "reg.sp", "reg.pc"};
        for (int c = 0; c < 6; ++c) {
            registers_->horizontalHeaderItem(c)->setToolTip(tips::key(keys[c]));
            auto* item = new QTableWidgetItem;
            item->setToolTip(tips::key(keys[c]));
            registers_->setItem(0, c, item);
        }
    }
    cpuLayout->addWidget(registers_);
    auto selectRegister = [this](int column) {
        const char* keys[] = {"reg.af", "reg.bc", "reg.de", "reg.hl", "reg.sp", "reg.pc"};
        if (column < 0 || column >= 6) return;
        information_->select({InformationKind::Register, informationTopicForGlossary(keys[column]), keys[column], 0, column});
        informationDock_->show();
    };
    connect(registers_, &QTableWidget::cellClicked, this, [selectRegister](int, int column) { selectRegister(column); });
    connect(registers_->horizontalHeader(), &QHeaderView::sectionClicked, this, selectRegister);
    auto* flagRow = new QHBoxLayout;
    flagRow->setSpacing(4);
    flagRow->addWidget(new QLabel("Flags"));
    const char* flagKeys[] = {"flag.z", "flag.n", "flag.h", "flag.c"};
    for (int i = 0; i < 4; ++i) {
        flagChips_[std::size_t(i)] = new QLabel;
        flagChips_[std::size_t(i)]->setObjectName(QString("flag%1").arg(QChar("ZNHC"[i])));
        flagChips_[std::size_t(i)]->setTextFormat(Qt::RichText);
        flagChips_[std::size_t(i)]->setToolTip(tips::key(flagKeys[i]));
        flagChips_[std::size_t(i)]->setProperty("informationFlag", i);
        flagRow->addWidget(flagChips_[std::size_t(i)]);
    }
    flags_ = new QLabel; flags_->setObjectName("flagsLabel");
    flags_->setToolTip(tips::make("A · the accumulator", q(glossary("reg.af").body)));
    flags_->setProperty("informationTopic", "registers");
    flagRow->addWidget(flags_);
    flagRow->addStretch();
    cpuLayout->addLayout(flagRow);
    instruction_ = label({}, "instructionLabel");
    instruction_->setFont(style::monospace());
    instruction_->setToolTip(tips::key("cpu.instruction"));
    instruction_->setProperty("informationTopic", "instructions");
    cpuLayout->addWidget(instruction_);
    cpuLayout->addStretch();
    cpuDock_ = makeDock("CPU · instruction boundary", "cpuDock", cpu);

    auto* mem = new QWidget;
    auto* memLayout = new QVBoxLayout(mem);
    memLayout->setContentsMargins(6, 6, 6, 6);
    auto* choices = new QHBoxLayout;
    region_ = new QComboBox;
    region_->setObjectName("memoryRegion");
    region_->setToolTip(tips::key("memory.region"));
    region_->addItem("WRAM · variables", 0xC000); region_->addItem("OAM · sprites", 0xFE00);
    region_->addItem("VRAM · tiles", 0x8000); region_->addItem("VRAM · tile map", 0x9800);
    region_->addItem("IO registers", 0xFF00); region_->addItem("HRAM · high RAM", 0xFF80);
    region_->addItem("ROM · bank 0 window", 0x0000); region_->addItem("ROM · switchable window", 0x4000);
    region_->addItem("Cartridge RAM", 0xA000);
    choices->addWidget(region_);
    address_ = new QLineEdit("C000"); address_->setObjectName("memoryAddress"); address_->setMaximumWidth(80);
    address_->setMaxLength(5); address_->setToolTip(tips::key("memory.address"));
    choices->addWidget(address_);
    auto* until = new QPushButton("Run until written · F9");
    until->setObjectName("runUntilWrittenButton");
    until->setToolTip(untilAction_->toolTip());
    connect(until, &QPushButton::clicked, this, [this] { runUntilWritten(selectedAddress_); });
    choices->addStretch();
    choices->addWidget(until);
    memLayout->addLayout(choices);
    window_ = label({}, "memoryWindowLabel");
    window_->setToolTip(tips::key("memory.window"));
    window_->setStyleSheet(QString("color: %1;").arg(style::muted.name()));
    memLayout->addWidget(window_);
    connect(region_, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        const auto a = std::uint16_t(region_->itemData(index).toUInt()); setMemoryBase(a); selectAddress(a);
    });
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
    memory_->setToolTip(tips::key("panel.memory"));
    selection_ = label({}, "selectionLabel");
    selection_->setToolTip(tips::key("memory.selection"));
    memLayout->addWidget(selection_);
    writer_ = label({}, "writerLabel");
    writer_->setTextFormat(Qt::RichText);
    writer_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(writer_, &QLabel::linkActivated, this, [this](const QString& link) {
        bool ok = false;
        const auto a = link.startsWith("addr:") ? link.mid(5).toUInt(&ok, 16) : 0u;
        if (ok && a <= 0xFFFF) { selectedEvent_.reset(); selectAddress(std::uint16_t(a)); }
    });
    memLayout->addWidget(writer_);
    connect(memory_, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column == 0) return;
        selectedEvent_.reset();
        selectAddress(std::uint16_t(memoryBase_ + row * 8 + column - 1));
    });
    memoryDock_ = makeDock("Memory", "memoryDock", mem);
    connect(memory_->horizontalHeader(), &QHeaderView::sectionClicked, this, [this] { showInformation("memory"); });

    cartridge_ = new CartridgePanel;
    batteryStatus_ = label({}, "batteryStatus");
    batteryStatus_->setTextFormat(Qt::RichText);
    batteryStatus_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    batteryStatus_->setToolTip(tips::key("cart.save"));
    batteryStatus_->setProperty("informationTopic", "battery");
    qobject_cast<QVBoxLayout*>(cartridge_->layout())->insertWidget(1, batteryStatus_);
    connect(batteryStatus_, &QLabel::linkActivated, this, [this](const QString& link) {
        if (link == "save:retry") saveBattery();
        else if (link == "save:elsewhere") saveBatteryAsAction_->trigger();
    });
    // Scrolls rather than overlapping when the dock is short (e.g. the minimum window size).
    auto* cartridgeScroll = new QScrollArea;
    cartridgeScroll->setWidget(cartridge_);
    cartridgeScroll->setWidgetResizable(true);
    cartridgeScroll->setFrameShape(QFrame::NoFrame);
    cartridgeDock_ = makeDock("Cartridge · banks", "cartridgeDock", cartridgeScroll);
    connect(cartridge_, &CartridgePanel::runUntilBankChange, this, [this] { runUntilBankChange(); });
    connect(cartridge_, &CartridgePanel::addressActivated, this, [this](std::uint16_t a) {
        memoryDock_->show(); memoryDock_->raise(); setMemoryBase(a); selectAddress(a);
        statusBar()->showMessage(QString("Showing %1 in the Memory panel.").arg(romLabel(a)), 5000);
    });

    tiles_ = new TileInspector;
    tilesDock_ = makeDock("Sprites and tiles", "tilesDock", tiles_);
    connect(tiles_, &TileInspector::spriteSelected, this, [this](int index) {
        game_->setSelectedSprite(index);
        if (index >= 0) { information_->select({InformationKind::Sprite, "oam", {}, 0, index}); informationDock_->show(); }
    });
    connect(tiles_, &TileInspector::tileSelected, this, [this](int tile) {
        information_->select({InformationKind::Tile, "tile-data", {}, 0, tile}); informationDock_->show();
    });
    connect(tiles_, &TileInspector::mapCellSelected, this, [this](std::uint16_t a, int) {
        selectAddress(a);
        information_->select({InformationKind::MapCell, "tile-map", {}, a}); informationDock_->show();
    });
    connect(tiles_, &TileInspector::addressActivated, this, [this](std::uint16_t a) {
        memoryDock_->show(); memoryDock_->raise();
        selectAddress(a);
        statusBar()->showMessage(QString("Selected %1 in the Memory panel.").arg(q(hex(a))), 4000);
    });

    map_ = new ActivityPanel;
    mapDock_ = makeDock("Memory map", "mapDock", map_);
    connect(map_, &ActivityPanel::clearRequested, this, [this] { engine_.clearActivityMap(); updatePanels(true); });
    connect(map_->view(), &ActivityMapView::addressActivated, this, [this](std::uint16_t a) {
        memoryDock_->show(); memoryDock_->raise();
        selectAddress(a);
        statusBar()->showMessage(QString("Selected %1 in the Memory panel.").arg(q(hex(a))), 4000);
    });

    auto* trace = new QWidget;
    auto* traceLayout = new QVBoxLayout(trace);
    traceLayout->setContentsMargins(6, 6, 6, 6);
    traceStatus_ = label({}, "traceStatusLabel");
    traceStatus_->setToolTip(tips::key("writes.status"));
    traceLayout->addWidget(traceStatus_);
    writes_ = table(0, 4, {"Tick end", "Instruction", "Address / name", "Before → after / effect"});
    writes_->setObjectName("writeTable");
    {
        const char* keys[] = {"writes.tick", "writes.instruction", "writes.address", "writes.values"};
        for (int c = 0; c < 4; ++c) writes_->horizontalHeaderItem(c)->setToolTip(tips::key(keys[c]));
    }
    writes_->setToolTip(tips::key("panel.writes"));
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
        information_->select({InformationKind::Write, "writes", {}, selectedAddress_, 0, id}); informationDock_->show();
    });
    writesDock_ = makeDock("Captured writes", "writesDock", trace);

    information_ = new InformationPanel;
    informationDock_ = makeDock("Selection info", "informationDock", information_);
    connect(information_, &InformationPanel::addressActivated, this, [this](std::uint16_t a) {
        memoryDock_->show(); memoryDock_->raise(); selectAddress(a);
    });
    connect(cartridge_->map(), &BankMap::bankSelected, this, [this](int bank, bool ram) {
        information_->select({InformationKind::Bank, ram ? "cart-ram" : "banks", {}, 0, bank, 0, ram});
        informationDock_->show();
    });

    viewMenu_->addSeparator();
    viewMenu_->addAction(spritesAction_);
    viewMenu_->addAction(changesAction_);
    viewMenu_->addSeparator();
    auto* layoutAction = new QAction("Reset layout", this);
    layoutAction->setObjectName("resetLayoutAction");
    layoutAction->setToolTip(tips::key("action.resetlayout"));
    connect(layoutAction, &QAction::triggered, this, [this] { resetLayout(); });
    viewMenu_->addAction(layoutAction);

    activityLabel_ = new QLabel; activityLabel_->setObjectName("activityLabel");
    activityLabel_->setToolTip(tips::key("ui.activity"));
    statusBar()->addPermanentWidget(activityLabel_);
}

void MainWindow::resetLayout() {
    const QList<QDockWidget*> all{systemDock_, cpuDock_, memoryDock_, cartridgeDock_, tilesDock_, mapDock_, writesDock_, informationDock_};
    for (auto* d : all) { d->setFloating(false); removeDockWidget(d); }
    addDockWidget(Qt::RightDockWidgetArea, systemDock_);
    addDockWidget(Qt::RightDockWidgetArea, cpuDock_);
    addDockWidget(Qt::RightDockWidgetArea, memoryDock_);
    tabifyDockWidget(memoryDock_, cartridgeDock_);
    tabifyDockWidget(cartridgeDock_, tilesDock_);
    tabifyDockWidget(tilesDock_, mapDock_);
    tabifyDockWidget(mapDock_, writesDock_);
    addDockWidget(Qt::BottomDockWidgetArea, informationDock_);
    for (auto* d : all) d->show();
    memoryDock_->raise();
    const int h = height();
    resizeDocks({systemDock_, cpuDock_, memoryDock_}, {std::max(150, h / 5), std::max(140, h / 6), h}, Qt::Vertical);
    // The game needs about 500 px for 3× scale; the inspectors get the rest.
    resizeDocks({systemDock_}, {std::max(560, width() - 520)}, Qt::Horizontal);
    // Leave the game enough height for 3× scale at the default size, 2× at the minimum.
    resizeDocks({informationDock_}, {std::clamp(h - 640, 220, 300)}, Qt::Vertical);
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
void MainWindow::warmBankDemo() {
    // Same convenience for the bank demo: stop once bank 1's pattern is on screen
    // (the first output after the LCD turns on is blank).
    int visibleFrames = 0;
    for (int i = 0; i < 16; ++i) {
        engine_.stepFrame();
        const auto state = engine_.snapshot();
        if (state.frameKind == "VBlank frame" && state.video.lcdc == 0x91 && engine_.inspect(bankdemo::current_bank) == 1 && ++visibleFrames == 2) break;
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
    auto target = runStartTick_ + ticksForNanoseconds(std::uint64_t(wall_.nsecsElapsed()));
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
    const auto where = romLabel(address);
    if (result.stop == WatchResult::Stop::CaptureOff) {
        statusBar()->showMessage("Turn on Capture writes to stop on a write.", 6000);
    } else if (result.stop == WatchResult::Stop::Dma) {
        const auto& d = *result.dma;
        statusBar()->showMessage(QString("Stopped after checking the OAM DMA request for %1–%2, including %3 (requested by %4; %5 instructions, %6 frames later; %7 of 160 bytes matched).")
            .arg(q(hex(d.sourceOf(0))), q(hex(d.sourceOf(oamBytes - 1))), where,
                 d.instruction ? q(hex(d.instruction->pc)) + " " + q(disassemble(*d.instruction)) : QString("an unrecorded instruction"))
            .arg(result.instructions).arg(result.frames).arg(d.matching), 10000);
    } else if (result.stop == WatchResult::Stop::Write) {
        const auto& w = *result.write;
        statusBar()->showMessage(QString("Stopped after the CPU write attempt to %1 by %2 (%3 instructions, %4 frames later).")
            .arg(where, w.instruction ? q(hex(w.instruction->pc)) + " " + q(disassemble(*w.instruction)) : QString("interrupt/wait work"))
            .arg(result.instructions).arg(result.frames), 10000);
    } else {
        statusBar()->showMessage(QString("No write to %1 within 60 frames (one emulated second); paused at the limit. "
            "Some writes need input, e.g. hold an arrow key.").arg(where), 10000);
    }
    return result;
}
WatchResult MainWindow::runUntilBankChange() {
    pause();
    selectedEvent_.reset();
    auto result = engine_.runUntilBankChange(60 * frameTicks);
    refresh();
    cartridgeDock_->show(); cartridgeDock_->raise();
    if (result.stop == WatchResult::Stop::BankChange) {
        QStringList changes;
        const auto& b = result.banksBefore; const auto& a = result.banksAfter;
        if (b.rom != a.rom) changes << QString("ROM bank at $4000: %1 → %2").arg(b.rom).arg(a.rom);
        if (b.rom0 != a.rom0) changes << QString("ROM bank at $0000: %1 → %2").arg(b.rom0).arg(a.rom0);
        if (b.ram != a.ram) changes << QString("RAM bank: %1 → %2").arg(b.ram).arg(a.ram);
        QString by = "capture off: the writer was not recorded";
        if (result.write) by = result.write->instruction ? q(hex(result.write->instruction->pc)) + " " + q(disassemble(*result.write->instruction))
                                                         : QString("core work without an opcode");
        statusBar()->showMessage(QString("Stopped after the bank change (%1) by %2; %3 instructions, %4 frames later.")
            .arg(changes.join(", "), by).arg(result.instructions).arg(result.frames), 12000);
    } else {
        statusBar()->showMessage(snapshot_.cartridge.info.banked()
            ? "No bank change within 60 frames (one emulated second); paused at the limit. Many games switch banks only when something new happens."
            : "This cartridge has no MBC, so its banks never change. Paused after one emulated second.", 10000);
    }
    return result;
}
void MainWindow::restart() {
    pause(); engine_.releaseButtons(); engine_.restart();
    if (snapshot_.teaching) warmTeaching();
    else if (snapshot_.bankDemo) warmBankDemo();
    afterLoad();
}
void MainWindow::loadTeaching() {
    pause(); if (!preserveBattery("opening another game")) return;
    engine_.loadTeaching(); warmTeaching();
    romName_ = "teaching game"; savePath_.clear();
    batteryProblem_.clear(); batteryBlocked_ = false;
    setWindowTitle("Console Observatory — DMG exploration lab");
    afterLoad(); setMemoryBase(0xC000);
}
void MainWindow::loadBankDemo() {
    const auto folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QByteArray bytes(reinterpret_cast<const char*>(bankdemo::rom.data()), qsizetype(bankdemo::rom.size()));
    if (!loadBytes(bytes, "bank-switching demo", folder.isEmpty() ? QString() : folder + "/bankdemo.sav")) return;
    setMemoryBase(0x4000);
    cartridgeDock_->show(); cartridgeDock_->raise();
}
void MainWindow::afterLoad() {
    // Nothing from the previous session is a "before" for the new one.
    previous_.reset(); selectedEvent_.reset(); snapshot_ = Snapshot{};
    // Overlays compare across time; a new session starts without them.
    spritesAction_->setChecked(false); changesAction_->setChecked(false);
    game_->setShowSprites(false); game_->setShowChanges(false); game_->setSelectedSprite(-1);
    tiles_->resetSelection();
    refresh();
    updateBatteryStatus();
    information_->reset();
}
void MainWindow::loadFile(const QString& path) {
    pause(); QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "Cannot open ROM", file.errorString()); return; }
    // Bound file reading before allocation: the largest cartridge is 8 MiB.
    if (file.size() < 0x150 || file.size() > qint64(maxRomBytes)) {
        QMessageBox::warning(this, "Not a Game Boy ROM", QString("%1 is %2 bytes. A Game Boy cartridge image is between 336 bytes (header) and 8 MiB.")
            .arg(QFileInfo(path).fileName()).arg(file.size()));
        return;
    }
    const QFileInfo info(path);
    loadBytes(file.readAll(), info.fileName(), info.absolutePath() + "/" + info.completeBaseName() + ".sav");
}
bool MainWindow::loadBytes(const QByteArray& bytes, const QString& name, const QString& savePath) {
    pause();
    const std::span rom(reinterpret_cast<const std::uint8_t*>(bytes.constData()), std::size_t(bytes.size()));
    const auto cart = describeCartridge(rom);
    if (!cart.supported) {
        QMessageBox::warning(this, "Unsupported cartridge", QString("%1 uses a %2 cartridge (type %3), which the SameBoy core does not emulate.")
            .arg(name, q(cart.typeName), q(hex(cart.type, 2))));
        return false;
    }
    if (!preserveBattery("opening another game")) return false;
    try {
        engine_.loadRom(rom);
    } catch (const std::exception& error) { QMessageBox::warning(this, "Cannot load ROM", error.what()); return false; }
    romName_ = name; savePath_ = cart.battery ? savePath : QString();
    batteryProblem_.clear(); batteryBlocked_ = false;
    const bool loadedSave = loadBatteryFile();
    const auto first = engine_.snapshot();
    if (first.teaching) warmTeaching();
    else if (first.bankDemo) warmBankDemo();
    setWindowTitle("Console Observatory — " + (cart.title.empty() ? name : q(cart.title) + " (" + name + ")"));
    afterLoad();
    setMemoryBase(0xC000);
    QString message = "Loaded " + name + ": " + q(cart.typeName) + QString(", %1 ROM banks").arg(cart.romBanks());
    if (!savePath_.isEmpty()) message += (loadedSave ? ", save loaded from " : batteryBlocked_ ? ", existing save protected: " : ", battery RAM saves to ") + QFileInfo(savePath_).fileName();
    if (cart.cgbOnly()) message += ". Marked Game Boy Color only: it may refuse to run on this monochrome system";
    else if (!first.teaching && !first.bankDemo) message += ". Paused at power-on; press F5 to run";
    statusBar()->showMessage(message + ".", 10000);
    return true;
}
bool MainWindow::loadBatteryFile() {
    if (savePath_.isEmpty()) return false;
    QFile file(savePath_);
    if (!file.exists()) return false;
    // A failed read must never turn into an automatic overwrite of that save.
    const auto maximum = qint64(engine_.batteryData().size());
    if (file.size() <= maximum && file.open(QIODevice::ReadOnly)) {
        const auto data = file.readAll();
        if (file.error() == QFileDevice::NoError && engine_.loadBattery(
                std::span(reinterpret_cast<const std::uint8_t*>(data.constData()), std::size_t(data.size())))) return true;
    }
    batteryBlocked_ = true;
    batteryProblem_ = "Existing save could not be loaded (unreadable or incompatible size). It has been protected from overwrite.";
    return false;
}
bool MainWindow::saveBattery() {
    if (savePath_.isEmpty() || batteryBlocked_) { updateBatteryStatus(); return false; }
    const auto data = engine_.batteryData();
    if (data.empty()) return false;
    QDir().mkpath(QFileInfo(savePath_).absolutePath());
    QSaveFile file(savePath_);
    if (!file.open(QIODevice::WriteOnly) || file.write(reinterpret_cast<const char*>(data.data()), qint64(data.size())) != qint64(data.size()) || !file.commit()) {
        batteryProblem_ = "Could not save to " + QDir::toNativeSeparators(savePath_) + ": " + file.errorString();
        updateBatteryStatus();
        statusBar()->showMessage(batteryProblem_ + ". Progress is still in memory; retry or use File → Save battery RAM as.");
        return false;
    }
    engine_.clearBatteryDirty();
    batteryProblem_.clear(); updateBatteryStatus();
    return true;
}
bool MainWindow::saveBatteryAs(const QString& path) {
    if (path.isEmpty()) return false;
    const auto previousPath = savePath_;
    const bool blocked = batteryBlocked_;
    savePath_ = QFileInfo(path).absoluteFilePath(); batteryBlocked_ = false;
    if (saveBattery()) return true;
    savePath_ = previousPath; batteryBlocked_ = blocked; updateBatteryStatus();
    return false;
}
bool MainWindow::preserveBattery(const QString& action) {
    if (!engine_.batteryDirty() || engine_.batteryData().empty()) return true;
    if (saveBattery()) return true;
    cartridgeDock_->show(); cartridgeDock_->raise();
    // Never trap the user: they may keep the game open, save elsewhere, or
    // knowingly discard the progress that cannot be written.
    QMessageBox box(QMessageBox::Warning, "Unsaved game progress",
        QString("This game's save could not be written%1, so its newest progress is only in memory.\n\n"
                "Save it to another file before %2, or discard that progress?")
            .arg(savePath_.isEmpty() ? QString() : " to " + QDir::toNativeSeparators(savePath_), action),
        QMessageBox::NoButton, this);
    box.setObjectName("unsavedProgressPrompt");
    if (!batteryProblem_.isEmpty()) box.setInformativeText(batteryProblem_);
    auto* elsewhere = box.addButton("Save elsewhere…", QMessageBox::AcceptRole);
    elsewhere->setObjectName("saveElsewhereButton");
    auto* discard = box.addButton("Discard progress", QMessageBox::DestructiveRole);
    discard->setObjectName("discardProgressButton");
    auto* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == discard) {
        engine_.clearBatteryDirty();
        updateBatteryStatus();
        statusBar()->showMessage("Discarded the unsaved battery progress.", 6000);
        return true;
    }
    if (box.clickedButton() == elsewhere) {
        const auto path = QFileDialog::getSaveFileName(this, "Save battery RAM elsewhere", savePath_, "Battery saves (*.sav);;All files (*)");
        if (!path.isEmpty() && saveBatteryAs(path)) return true;
    }
    statusBar()->showMessage("The current game is still open with its unsaved progress. Retry saving or use File → Save battery RAM as.");
    return false;
}
void MainWindow::updateBatteryStatus() {
    if (!batteryStatus_) return;
    const bool hasBattery = snapshot_.cartridge.info.battery && (snapshot_.cartridge.ramBytes || snapshot_.cartridge.info.timer);
    saveBatteryAction_->setEnabled(hasBattery && !savePath_.isEmpty() && !batteryBlocked_);
    saveBatteryAsAction_->setEnabled(hasBattery);
    batteryStatus_->setVisible(hasBattery);
    if (!hasBattery) return;
    QString text = QString("<b>Battery save: %1</b><br>%2")
        .arg(batteryBlocked_ ? "protected" : engine_.batteryDirty() ? "unsaved changes" : "saved / no pending changes",
             QDir::toNativeSeparators(savePath_).toHtmlEscaped());
    if (!batteryProblem_.isEmpty()) text += "<br>" + batteryProblem_.toHtmlEscaped();
    // Links in the accent colour: Qt's default link blue is unreadable on the dark theme.
    const auto link = [](const char* href, const char* label) {
        return QString("<a href='%1' style='color:%2'>%3</a>").arg(href, style::accent.name(), label);
    };
    if (!batteryBlocked_) text += "<br>" + link("save:retry", "Save now / retry") + " · ";
    else text += "<br>Automatic saving is disabled for this file. ";
    text += link("save:elsewhere", "Save elsewhere");
    batteryStatus_->setText(text);
}
QString MainWindow::romLabel(std::uint16_t address) const {
    const auto name = addressName(address, programOf(snapshot_));
    auto text = q(hex(address)) + (name.empty() ? QString() : " " + q(name));
    const auto& c = snapshot_.cartridge;
    if (address < 0x4000 && c.info.banked()) text += QString(" (ROM bank %1)").arg(c.banks.rom0);
    else if (address >= 0x4000 && address < 0x8000) text += QString(" (ROM bank %1)").arg(c.banks.rom);
    else if (address >= 0xA000 && address < 0xC000 && c.ramBytes) text += QString(" (RAM bank %1)").arg(c.banks.ram);
    return text;
}
void MainWindow::setMemoryBase(std::uint16_t base) {
    memoryBase_ = std::min<std::uint16_t>(base & 0xFFF8, 0xFF80);
    selectedAddress_ = std::max(memoryBase_, std::min<std::uint16_t>(base, memoryBase_ + memoryWindow - 1));
    address_->setText(q(hex(memoryBase_)).mid(1));
    // Show which region the window is in; blank when it is none of the listed ones.
    struct Range { std::uint16_t first, last; };
    static const Range ranges[] = {{0xC000, 0xDFFF}, {0xFE00, 0xFE9F}, {0x8000, 0x97FF}, {0x9800, 0x9FFF}, {0xFF00, 0xFF7F}, {0xFF80, 0xFFFF},
                                   {0x0000, 0x3FFF}, {0x4000, 0x7FFF}, {0xA000, 0xBFFF}};
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
    information_->select({InformationKind::Memory, informationTopicForAddress(address), {}, address});
    informationDock_->show();
}
void MainWindow::updateSelection() {
    if (selectedAddress_ >= memoryBase_ && selectedAddress_ < memoryBase_ + memoryWindow) {
        auto offset = selectedAddress_ - memoryBase_;
        memory_->setCurrentCell(offset / 8, offset % 8 + 1);
        const auto value = snapshot_.memory[offset];
        const auto name = addressName(selectedAddress_, programOf(snapshot_));
        selection_->setText(QString("<b>%1</b>%2 · %3 · value %4")
            .arg(q(hex(selectedAddress_)), name.empty() ? QString() : " " + q(name), q(regionName(selectedAddress_)),
                 snapshot_.memoryAvailable[offset] ? QString("%1 (%2)").arg(q(hex(value, 2))).arg(value) : QString("— (no storage)")));
    }
    // Cartridge windows: which bank of the file the CPU sees here right now.
    const auto& c = snapshot_.cartridge;
    const auto a = memoryBase_;
    QString text;
    if (a < 0x8000) {
        const std::uint32_t bank = a < 0x4000 ? c.banks.rom0 : c.banks.rom;
        const auto offset = bank * 0x4000u + (a & 0x3FFF);
        text = QString("$%1–$%2 shows ROM bank %3 (file offset %4). ")
            .arg(a < 0x4000 ? "0000" : "4000", a < 0x4000 ? "3FFF" : "7FFF").arg(bank).arg(q(hex(offset, 6)));
        if (a < 0x100 && c.bootMapped) text += "The boot program covers $0000–$00FF until it writes FF50; the table shows it. ";
        text += c.info.banked() ? (a < 0x4000 ? "This bank is normally fixed." : "The MBC chooses this bank; it changes when the program writes a bank number to $2000–$3FFF.")
                                : "No MBC: this mapping never changes.";
        if (c.info.mbc == Mbc::Mmm01) text += " MMM01 rearranges ROM in emulator memory, so the file offset may differ.";
    } else if (a >= 0xA000 && a < 0xC000) {
        if (!c.ramBytes) text = "This cartridge has no RAM: $A000–$BFFF has no storage.";
        else {
            text = QString("$A000–$BFFF shows cartridge RAM bank %1 (save-data offset %2)%3. While the program has RAM disabled, "
                           "CPU reads here do not reach it; this view shows storage regardless.")
                .arg(c.banks.ram).arg(q(hex(std::uint32_t(c.banks.ram) * 0x2000u % std::max<std::size_t>(c.ramBytes, 1), 6)))
                .arg(c.info.battery ? ", battery-backed" : "");
            if (c.info.timer) text += " With a clock register selected, reads return the clock instead.";
        }
    }
    window_->setText(text);
    window_->setVisible(!text.isEmpty());
}
void MainWindow::showInformation(const std::string& topic) {
    information_->showTopic(topic);
    informationDock_->show(); informationDock_->raise();
}
void MainWindow::refresh() {
    if (snapshot_.ticks) previous_ = snapshot_;
    snapshot_ = engine_.snapshot(memoryBase_);
    if (previous_ && previous_->ticks == snapshot_.ticks && previous_->traceEnabled == snapshot_.traceEnabled &&
        snapshot_.activity.startTicks == snapshot_.activity.endTicks) snapshot_.activity = previous_->activity;
    const auto& s = snapshot_;
    badge_->setText(running_ ? "RUNNING" : "PAUSED");
    badge_->setStyleSheet(running_ ? "background:#2c6b5c; color:#e9fff6;" : "background:#57472c; color:#ffe19a;");
    cursor_->setText(QString("State now: t = %1 ticks\ninstruction #%2 · output #%3").arg(s.ticks).arg(s.instructions).arg(s.frames));
    const std::array<std::uint16_t, 6> values{s.registers.af, s.registers.bc, s.registers.de, s.registers.hl, s.registers.sp, s.registers.pc};
    std::array<std::uint16_t, 6> old{};
    if (previous_) old = {previous_->registers.af, previous_->registers.bc, previous_->registers.de, previous_->registers.hl, previous_->registers.sp, previous_->registers.pc};
    for (int i = 0; i < 6; ++i) {
        const bool changed = previous_ && values[i] != old[i];
        cell(registers_, 0, i, q(hex(values[i])) + (changed ? " Δ" : ""), changed);
    }
    const auto f = s.registers.af;
    const char* flagNames[] = {"Z", "N", "H", "C"};
    for (int i = 0; i < 4; ++i) flagChips_[std::size_t(i)]->setText(flagChip(flagNames[i], f & (0x80 >> i)));
    flags_->setText(QString("  A = %1 (%2)").arg(q(hex(f >> 8, 2))).arg(f >> 8));
    // On banked cartridges, the same PC can hold different code: say which bank.
    auto where = [&s](const Instruction& i) {
        const bool banked = s.cartridge.info.banked() && i.pc < 0x8000 && !(s.cartridge.bootMapped && i.pc < 0x100);
        return q(hex(i.pc)) + (banked ? QString(" bank %1").arg(i.bank) : QString());
    };
    auto text = QString("Next  %1  %2   ← storage at PC, not yet executed\n").arg(where(s.next), q(disassemble(s.next)));
    if (s.lastExecuted) text += QString("Last  %1  %2\n").arg(where(*s.lastExecuted), q(disassemble(*s.lastExecuted)));
    const auto note = s.lastExecuted ? instructionNote(s.lastExecuted->pc, s.lastExecuted->bank, programOf(s)) : std::string();
    if (!note.empty()) text += q(note);
    instruction_->setText(text.trimmed());
    game_->setFrame(s);
    // One line, so the game keeps 2x scale at the minimum window size; the
    // exact completion tick lives in the tooltip.
    const auto age = s.ticks - s.frameBoundaryTicks;
    frameLabel_->setText(QString("Picture: output #%1 · %2 · drawn %3 ms before now")
        .arg(s.frames).arg(q(s.frameKind)).arg(double(age) * 1000 / ticksPerSecond, 0, 'f', 2));
    frameLabel_->setToolTip(tips::make(q(glossary("ui.frame").title), q(glossary("ui.frame").body),
        QString("Output #%1 completed at t = %2 ticks; the CPU and memory panels show t = %3, %4 CPU cycles later.")
            .arg(s.frames).arg(s.frameBoundaryTicks).arg(s.ticks).arg(age / 2)));
    for (int row = 0; row < 16; ++row) {
        cell(memory_, row, 0, q(hex(memoryBase_ + row * 8)));
        for (int c = 0; c < 8; ++c) {
            auto index = row * 8 + c;
            bool changed = previous_ && previous_->memoryBase == s.memoryBase && previous_->memory[index] != s.memory[index];
            cell(memory_, row, c + 1, s.memoryAvailable[index] ? q(hex(s.memory[index], 2)).mid(1) + (changed ? " Δ" : "") : "—", changed);
            auto a = std::uint16_t(memoryBase_ + index);
            memory_->item(row, c + 1)->setToolTip(byteTip(a, romLabel(a), s.memory[index], s.memoryAvailable[index], changed));
        }
    }
    const auto& a = s.activity;
    activityLabel_->setText(QString("%1 interval: %2 opcodes · writes VRAM %3 / WRAM %4 / OAM %5 / IO %6%7%8")
        .arg(running_ ? "Live" : "Last").arg(a.instructions).arg(a.writes[1]).arg(a.writes[2]).arg(a.writes[3]).arg(a.writes[4])
        .arg(a.dmaTransfers ? QString(" · OAM DMA %1").arg(a.dmaTransfers) : QString())
        .arg(s.traceEnabled ? "" : " (capture off)"));
    diagram_->setSnapshot(s);
    updateBatteryStatus();
    updatePanels(!running_);
    updateSelection(); updateWriter();
    information_->setSnapshot(s);
}
void MainWindow::updatePanels(bool force) {
    const auto& s = snapshot_;
    if (force || shown(writesDock_)) {
        const auto rows = int(std::min<std::size_t>(s.writes.size(), 64));
        writes_->setRowCount(rows);
        for (int row = 0; row < rows; ++row) {
            const auto& e = s.writes[s.writes.size() - 1 - row];
            cell(writes_, row, 0, QString::number(e.endTicks)); writes_->item(row, 0)->setData(Qt::UserRole, qulonglong(e.id));
            cell(writes_, row, 1, e.instruction ? q(hex(e.instruction->pc)) +
                 (e.instruction->pc < 0x8000 && s.cartridge.info.banked() ? QString(" bank %1").arg(e.instruction->bank) : QString()) +
                 "  " + q(disassemble(*e.instruction)) : "no opcode (interrupt/wait)");
            if (e.address < 0x8000) {
                // ROM cannot be written: on a banked cartridge this is an MBC command.
                const auto reg = mbcRegisterName(s.cartridge.info.mbc, e.address);
                cell(writes_, row, 2, q(hex(e.address)) + " ← " + q(hex(e.requested, 2)) + (reg.empty() ? QString() : " " + q(reg)));
                cell(writes_, row, 3, bankEffect(e, s.cartridge.info), e.banksBefore != e.banksAfter);
            } else {
                cell(writes_, row, 2, q(hex(e.address)) + " " + q(addressName(e.address, programOf(s))) +
                     (e.address >= 0xA000 && e.address < 0xC000 ? QString(" (recorded RAM bank %1)").arg(e.bank) : QString()));
                cell(writes_, row, 3, (e.valuesAvailable ? q(hex(e.before, 2)) : "—") + " → " + (e.valuesAvailable ? q(hex(e.after, 2)) : "—") +
                     (e.address == 0xFF46 ? QString(" · starts OAM DMA from %1").arg(q(hex(std::uint16_t(e.requested << 8)))) : QString()),
                     e.address == 0xFF46);
            }
        }
        traceStatus_->setText(QString("%1 · %2/%3 retained · %4 evicted (earlier history incomplete) · from t=%5. "
                                      "Newest 64 shown. Selecting one keeps the current state at t=%6; it is evidence, not a replay.")
            .arg(s.traceEnabled ? "Capture ON" : "Capture OFF").arg(s.writes.size()).arg(engine_.traceCapacity())
            .arg(s.evictedWrites).arg(s.oldestRetainedTick).arg(s.ticks));
    }
    if (force || shown(tilesDock_)) tiles_->setSnapshot(s);
    const bool map = force || shown(mapDock_), cart = force || shown(cartridgeDock_);
    if (map || cart) engine_.activityMap(activity_);
    if (map) map_->setMap(activity_, programOf(s));
    if (cart) cartridge_->setSnapshot(s, activity_);
}
namespace {
QString html(const QString& plain) { return plain.toHtmlEscaped().replace('\n', "<br>"); }
QString addressLink(std::uint16_t a) {
    return QString("<a href='addr:%1' style='color:%2'>%3</a>").arg(a, 4, 16, QChar('0')).arg(style::accent.name(), q(hex(a)));
}
}
void MainWindow::updateWriter() {
    const WriteEvent* found = nullptr;
    if (selectedEvent_) {
        for (const auto& e : snapshot_.writes) if (e.id == *selectedEvent_) found = &e;
    } else {
        for (auto i = snapshot_.writes.rbegin(); i != snapshot_.writes.rend(); ++i) {
            if (writerMatches(*i, selectedAddress_, snapshot_.cartridge)) { found = &*i; break; }
        }
    }
    // Retained CPU attempts and DMA requests provide candidate evidence. A
    // later OAM/source comparison does not establish per-byte provenance.
    const auto a = canonicalAddress(selectedAddress_);
    if (!selectedEvent_ && a >= 0xFE00 && a < 0xFE00 + oamBytes) {
        const auto i = std::size_t(a - 0xFE00);
        auto requestedBy = [](const DmaTransfer& d) {
            return d.instruction ? q(hex(d.instruction->pc)) + "  " + q(disassemble(*d.instruction)) : QString("an unrecorded instruction");
        };
        if (const auto& c = snapshot_.dmaCopying; c && (!found || found->endTicks <= c->requestEndTicks || !c->requestEndTicks)) {
            writer_->setText(html(QString("OAM DMA in progress: %1 asked the hardware to copy %2–%3 into OAM at t=%4. The copy takes "
                                          "160 machine cycles while the CPU keeps running; step a few more instructions to see it land.")
                .arg(requestedBy(*c), q(hex(c->sourceOf(0))), q(hex(c->sourceOf(oamBytes - 1)))).arg(c->requestStartTicks)));
            return;
        }
        const DmaTransfer* dma = snapshot_.dma.empty() ? nullptr : &snapshot_.dma.back();
        if (dma && (!found || found->endTicks <= dma->requestEndTicks)) {
            const auto& d = *dma;
            QString text = QString("<b>Most recent OAM DMA evidence</b>. %1 wrote %2 to $FF46 "
                                   "in [%3, %4] ticks, asking for %5–%6 to be copied into $FE00–$FE9F. ")
                .arg(html(requestedBy(d)), q(hex(d.page, 2))).arg(d.requestStartTicks).arg(d.requestEndTicks)
                .arg(q(hex(d.sourceOf(0))), q(hex(d.sourceOf(oamBytes - 1))));
            if (d.status == DmaTransfer::Status::Restarted) {
                text += QString("A newer request restarted it at t=%1; this record does not establish how many bytes transferred.").arg(d.checkedTicks);
            } else {
                text += QString("Checked at t=%1: %2 of 160 OAM bytes equal their source%3.").arg(d.checkedTicks).arg(d.matching)
                    .arg(d.matching == int(oamBytes) ? QString() : QString(" (the comparison does not establish the cause of a mismatch)"));
            }
            text += QString("<br>This copy requested source %1 for this byte. OAM held %2 at the request and %3 at the check. "
                            "Click %1 to inspect the source's current retained write evidence; this is not a per-byte transfer trace.")
                .arg(addressLink(d.sourceOf(i)), q(hex(d.before[i], 2)), q(hex(d.after[i], 2)));
            writer_->setText(text);
            return;
        }
    }
    if (!found) {
        writer_->setText(html(snapshot_.traceEnabled
            ? "Last writer: no retained evidence. The write may predate the capture window, or happened by a path the CPU "
              "write hook cannot see."
            : "Last writer: Capture writes is off, so writes are not being recorded."));
        return;
    }
    const auto& e = *found;
    auto text = QString(selectedEvent_ ? "Selected captured attempt (interval [%1, %2] ticks): " :
                                        "Last retained CPU attempt (interval [%1, %2] ticks): ").arg(e.startTicks).arg(e.endTicks);
    text += e.instruction ? q(hex(e.instruction->pc)) + "  " + q(disassemble(*e.instruction)) : "core work without an opcode (e.g. interrupt service)";
    if (e.instruction && e.instruction->pc < 0x8000 && snapshot_.cartridge.info.banked())
        text += QString(" (executing ROM bank %1)").arg(e.instruction->bank);
    if (e.address >= 0xA000 && e.address < 0xC000) {
        text += QString("\nRecorded RAM bank %1; selected address is in RAM bank %2.").arg(e.bank).arg(snapshot_.cartridge.banks.ram);
    }
    if (e.address < 0x8000) {
        const auto& info = snapshot_.cartridge.info;
        const auto reg = mbcRegisterName(info.mbc, e.address);
        text += QString("\nRequested %1. ROM itself never changes: %2. Effect: %3.")
            .arg(q(hex(e.requested, 2)), info.banked() ? "the " + q(mbcName(info.mbc)) + " chip reads this as a command" + (reg.empty() ? QString() : " to its " + q(reg))
                                                        : QString("with no MBC the write goes nowhere"),
                 bankEffect(e, info));
    } else {
        text += QString("\nBefore %1 · requested %2 · after %3. %4")
            .arg(e.valuesAvailable ? q(hex(e.before, 2)) : "—", q(hex(e.requested, 2)), e.valuesAvailable ? q(hex(e.after, 2)) : "—",
                 e.physicalStorage ? "Physical storage observed." : "Raw register/ROM storage; not proof of acceptance.");
        if (e.address == 0xFF46) text += QString("\nThis starts an OAM DMA copy of %1–%2 into $FE00–$FE9F.")
            .arg(q(hex(std::uint16_t(e.requested << 8))), q(hex(std::uint16_t(e.requested << 8 | 0x9F))));
    }
    if (e.instruction) {
        const auto note = instructionNote(e.instruction->pc, e.instruction->bank, programOf(snapshot_));
        if (!note.empty()) text += "\n" + q(note);
    }
    writer_->setText(html(text));
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
void MainWindow::closeEvent(QCloseEvent* event) {
    pause();
    if (!preserveBattery("closing")) { event->ignore(); return; }
    QMainWindow::closeEvent(event);
}
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (droppedFile(event->mimeData())) event->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent* event) {
    if (auto path = droppedFile(event->mimeData())) { event->acceptProposedAction(); loadFile(*path); }
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
    if (event->type() == QEvent::MouseButtonRelease) {
        if (auto* widget = qobject_cast<QWidget*>(object); widget && ownsKeyboard(widget)) {
            const auto flag = widget->property("informationFlag");
            if (flag.isValid()) {
                const int i = flag.toInt(); const char* keys[] = {"flag.z", "flag.n", "flag.h", "flag.c"};
                if (i >= 0 && i < 4) { information_->select({InformationKind::Flag, "flags", keys[i], 0, i}); informationDock_->show(); }
            } else if (const auto topic = widget->property("informationTopic").toString(); !topic.isEmpty()) {
                showInformation(topic.toStdString());
            }
        }
    }
    if (event->type() == QEvent::WindowDeactivate && object == this) { engine_.releaseButtons(); }
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto* widget = qobject_cast<QWidget*>(object);
        if (!ownsKeyboard(widget)) return QMainWindow::eventFilter(object, event);
        auto* key = static_cast<QKeyEvent*>(event);
        auto button = buttonFor(key->key());
        // Text fields keep their keys, and Enter still activates a focused button.
        // Always release a game key on key-up even if focus changed while it was held.
        const bool textField = qobject_cast<QLineEdit*>(widget) || (information_ && information_->isAncestorOf(widget));
        const bool enterOnButton = key->key() == Qt::Key_Return && qobject_cast<QAbstractButton*>(widget);
        if (button && !key->isAutoRepeat() && (event->type() == QEvent::KeyRelease || (!textField && !enterOnButton))) {
            engine_.setButton(*button, event->type() == QEvent::KeyPress);
            return !enterOnButton && !textField;
        }
    }
    return QMainWindow::eventFilter(object, event);
}
} // namespace observatory
