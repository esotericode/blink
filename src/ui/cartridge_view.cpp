#include "ui/cartridge_view.hpp"
#include "ui/style.hpp"
#include "ui/tooltip.hpp"
#include "teaching/glossary.hpp"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFontMetricsF>
#include <QHelpEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTableWidget>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace observatory {
namespace {
using style::q;
const QColor rom0Colour("#4fd1c5");
QString kib(std::size_t bytes) {
    return bytes >= 1024 * 1024 ? QString("%1 MiB").arg(double(bytes) / (1024 * 1024), 0, 'g', 3)
                                : QString("%1 KiB").arg(double(bytes) / 1024, 0, 'g', 4);
}
// Prefers a few rows, so the panel fits a default-sized dock without scrolling.
class CompactTable : public QTableWidget {
public:
    using QTableWidget::QTableWidget;
    QSize sizeHint() const override { return {420, 120}; }
    QSize minimumSizeHint() const override { return {200, 96}; }
};
QColor mix(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}
}

QString bankEffect(const WriteEvent& w, const CartridgeInfo& info) {
    if (w.address >= 0x8000) return {};
    if (!info.banked()) return "ignored: this cartridge has no MBC";
    QStringList parts;
    if (w.banksBefore.rom != w.banksAfter.rom) parts << QString("ROM bank %1 → %2").arg(w.banksBefore.rom).arg(w.banksAfter.rom);
    if (w.banksBefore.rom0 != w.banksAfter.rom0) parts << QString("bank at $0000 %1 → %2").arg(w.banksBefore.rom0).arg(w.banksAfter.rom0);
    if (w.banksBefore.ram != w.banksAfter.ram) parts << QString("RAM bank %1 → %2").arg(w.banksBefore.ram).arg(w.banksAfter.ram);
    if (!parts.isEmpty()) return parts.join(", ");
    const bool enableRegister = info.mbc == Mbc::Mbc2 ? (w.address < 0x4000 && !(w.address & 0x100)) : w.address < 0x2000;
    if (enableRegister && info.mbc != Mbc::Mbc7) {
        return (w.requested & 0x0F) == 0x0A ? "requests RAM enable ($0A)" : "requests RAM disable";
    }
    return "no bank change";
}

BankMap::BankMap(QWidget* parent) : QWidget(parent) {
    setObjectName("bankMap");
    setMouseTracking(true);
    tips::setCustom(this);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void BankMap::setData(const CartridgeState& cartridge, const ActivityMap& activity) {
    cartridge_ = cartridge;
    executions_ = activity.bankExecutions;
    bootExecutions_ = activity.bootExecutions;
    const auto max = executions_.empty() ? 0u : *std::max_element(executions_.begin(), executions_.end());
    logMax_ = max ? std::log1p(double(max)) : 0.0;
    update();
}
int BankMap::romBanks() const { return std::max(2, int(cartridge_.romBytes / 0x4000)); }
int BankMap::ramBanks() const {
    return cartridge_.ramBytes ? std::max(1, int(cartridge_.ramBytes / 0x2000)) : 0;
}
BankMap::Layout BankMap::layout() const {
    Layout l;
    const double margin = 8, windowWidth = std::clamp(width() * 0.27, 150.0, 210.0), gap = 56;
    // Window boxes shrink with the widget's height, down to two text lines.
    const double box = std::clamp((height() - 26 - 22 - 12) / 3.0, 36.0, 44.0);
    l.rom0Window = QRectF(margin, 26, windowWidth, box);
    l.romWindow = QRectF(margin, 26 + box + 6, windowWidth, box);
    l.ramWindow = QRectF(margin, 26 + 2 * (box + 6), windowWidth, box);
    const double x = margin + windowWidth + gap, available = std::max(60.0, width() - x - margin);
    const int banks = romBanks(), ram = ramBanks();
    const double ramHeight = ram ? 54 : 0, height = std::max(40.0, this->height() - 26 - ramHeight - 22);
    l.cell = std::clamp(available / std::min(banks, 16), 9.0, 44.0);
    for (int pass = 0; pass < 3; ++pass) {
        l.columns = std::max(1, std::min(banks, int(available / l.cell)));
        const int rows = (banks + l.columns - 1) / l.columns;
        if (rows * l.cell <= height) break;
        l.cell = std::max(6.0, height / rows);
    }
    const int rows = (banks + l.columns - 1) / l.columns;
    l.romArea = QRectF(x, 26, l.columns * l.cell, rows * l.cell);
    if (ram) {
        l.ramCell = std::min({l.cell, 30.0, available / ram});
        l.ramColumns = std::max(1, std::min(ram, int(available / l.ramCell)));
        l.ramArea = QRectF(x, l.romArea.bottom() + 24, l.ramColumns * l.ramCell, ((ram + l.ramColumns - 1) / l.ramColumns) * l.ramCell);
    }
    return l;
}
QRectF BankMap::romCell(const Layout& l, int bank) const {
    return QRectF(l.romArea.x() + (bank % l.columns) * l.cell, l.romArea.y() + (bank / l.columns) * l.cell, l.cell - 2, l.cell - 2);
}
QRectF BankMap::ramCell(const Layout& l, int bank) const {
    return QRectF(l.ramArea.x() + (bank % l.ramColumns) * l.ramCell, l.ramArea.y() + (bank / l.ramColumns) * l.ramCell, l.ramCell - 2, l.ramCell - 2);
}
void BankMap::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), style::window);
    const auto l = layout();
    const auto& banks = cartridge_.banks;
    const bool banked = cartridge_.info.banked();
    auto small = font(); small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
    auto bold = small; bold.setBold(true);
    painter.setFont(bold);
    painter.setPen(style::muted);
    painter.drawText(QRectF(l.rom0Window.x(), 4, l.rom0Window.width(), 18), Qt::AlignLeft | Qt::AlignVCenter, "What the CPU sees");
    painter.drawText(QRectF(l.romArea.x(), 4, width() - l.romArea.x() - 8, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     QString("Cartridge ROM: %1 banks × 16 KiB").arg(romBanks()));

    // Connectors first, so the boxes draw over their ends.
    auto connect = [&](const QRectF& from, const QRectF& to, const QColor& colour, double width) {
        QPainterPath path;
        const QPointF start(from.right(), from.center().y()), end(to.left(), to.center().y());
        path.moveTo(start);
        path.cubicTo(start + QPointF(36, 0), end - QPointF(36, 0), end);
        painter.setPen(QPen(colour, width));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    };
    const int rom0 = std::min<int>(banks.rom0, romBanks() - 1), rom = std::min<int>(banks.rom, romBanks() - 1);
    connect(l.rom0Window, romCell(l, rom0), rom0Colour, 2);
    connect(l.romWindow, romCell(l, rom), style::highlight, 3);
    if (ramBanks()) connect(l.ramWindow, ramCell(l, std::min<int>(banks.ram, ramBanks() - 1)), style::write, 2);

    auto window = [&](const QRectF& box, const QString& title, const QString& detail, const QColor& edge, bool present) {
        painter.setPen(QPen(present ? edge : style::border, present ? 2 : 1));
        painter.setBrush(style::panel);
        painter.drawRoundedRect(box, 5, 5);
        painter.setFont(bold);
        painter.setPen(present ? edge : style::muted);
        painter.drawText(box.adjusted(8, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop, title);
        painter.setFont(small);
        painter.setPen(present ? style::text : style::muted);
        painter.drawText(box.adjusted(8, 18, -4, 0), Qt::AlignLeft | Qt::AlignTop, detail);
    };
    window(l.rom0Window, "$0000–$3FFF",
           cartridge_.bootMapped ? QString("ROM bank %1 · boot at $0000–$00FF").arg(banks.rom0) : QString("ROM bank %1 · fixed").arg(banks.rom0),
           rom0Colour, true);
    window(l.romWindow, "$4000–$7FFF", QString("ROM bank %1 · %2").arg(banks.rom).arg(banked ? "switchable" : "fixed"), style::highlight, true);
    window(l.ramWindow, "$A000–$BFFF", ramBanks() ? QString("RAM bank %1").arg(banks.ram) : QString("no cartridge RAM"), style::write, ramBanks() > 0);

    painter.setFont(small);
    for (int bank = 0; bank < romBanks(); ++bank) {
        const auto box = romCell(l, bank);
        const auto count = bank < int(executions_.size()) ? executions_[std::size_t(bank)] : 0u;
        const auto fill = count && logMax_ > 0 ? mix(style::panel, style::execute, 0.25 + 0.75 * std::log1p(double(count)) / logMax_) : style::panel;
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRect(box);
        const bool atRom = bank == rom, atRom0 = bank == rom0;
        painter.setBrush(Qt::NoBrush);
        painter.setPen(atRom ? QPen(style::highlight, 3) : atRom0 ? QPen(rom0Colour, 2) : QPen(style::border, 1));
        painter.drawRect(box);
        if (box.width() >= 16) {
            painter.setPen(fill.lightness() > 120 ? QColor("#10171d") : style::text);
            painter.drawText(box, Qt::AlignCenter, QString::number(bank));
        }
    }
    if (ramBanks()) {
        painter.setFont(bold);
        painter.setPen(style::muted);
        painter.drawText(QRectF(l.ramArea.x(), l.ramArea.y() - 20, width() - l.ramArea.x() - 8, 18), Qt::AlignLeft | Qt::AlignVCenter,
                         QString("Cartridge RAM: %1 × %2%3").arg(ramBanks()).arg(cartridge_.ramBytes < 0x2000 ? kib(cartridge_.ramBytes) : QString("8 KiB"))
                             .arg(cartridge_.info.battery ? " · battery-backed" : ""));
        painter.setFont(small);
        for (int bank = 0; bank < ramBanks(); ++bank) {
            const auto box = ramCell(l, bank);
            painter.setBrush(style::panel);
            painter.setPen(bank == banks.ram ? QPen(style::write, 3) : QPen(style::border, 1));
            painter.drawRect(box);
            if (box.width() >= 16) { painter.setPen(style::text); painter.drawText(box, Qt::AlignCenter, QString::number(bank)); }
        }
    }
    painter.setPen(style::muted);
    const QFontMetricsF metrics(small);
    painter.drawText(QRectF(8, height() - 18, width() - 16, 16), Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText("Lines: the bank each window shows now · blue fill: opcodes run from that bank (brighter = more)",
                                        Qt::ElideRight, width() - 16));
}
void BankMap::mousePressEvent(QMouseEvent* event) {
    const auto l = layout();
    const auto p = event->position();
    if (l.rom0Window.contains(p)) emit windowActivated(0x0000);
    else if (l.romWindow.contains(p)) emit windowActivated(0x4000);
    else if (l.ramWindow.contains(p) && ramBanks()) emit windowActivated(0xA000);
    else {
        for (int bank = 0; bank < romBanks(); ++bank) {
            if (!romCell(l, bank).contains(p)) continue;
            if (bank == cartridge_.banks.rom) emit windowActivated(0x4000);
            else if (bank == cartridge_.banks.rom0) emit windowActivated(0x0000);
            emit bankSelected(bank, false);
            return;
        }
        for (int bank = 0; bank < ramBanks(); ++bank) {
            if (!ramCell(l, bank).contains(p)) continue;
            if (bank == cartridge_.banks.ram) emit windowActivated(0xA000);
            emit bankSelected(bank, true); return;
        }
    }
}
bool BankMap::event(QEvent* event) {
    if (event->type() != QEvent::ToolTip) return QWidget::event(event);
    auto* help = static_cast<QHelpEvent*>(event);
    const auto l = layout();
    const QPointF p = help->pos();
    QString text;
    QRect area = rect();
    for (int bank = 0; bank < romBanks() && text.isEmpty(); ++bank) {
        if (!romCell(l, bank).contains(p)) continue;
        const auto count = bank < int(executions_.size()) ? executions_[std::size_t(bank)] : 0u;
        QString body = QString("16 KiB of the cartridge, bytes %1–%2 of the ROM file. %3 instructions ran from it since the map was cleared.")
            .arg(q(hex(std::uint64_t(bank) * 0x4000, 6)), q(hex(std::uint64_t(bank) * 0x4000 + 0x3FFF, 6))).arg(count);
        QString hint;
        if (bank == cartridge_.banks.rom) { body += " It is mapped at $4000–$7FFF right now."; hint = "Click to view it in the Memory panel."; }
        else if (bank == cartridge_.banks.rom0) { body += " It is mapped at $0000–$3FFF right now."; hint = "Click to view it in the Memory panel."; }
        else body += " Not mapped: the CPU can't see it until the program selects it.";
        if (cartridge_.info.mbc == Mbc::Mmm01) body += " (MMM01 rearranges memory, so file offsets may differ.)";
        text = tips::make(QString("ROM bank %1").arg(bank), body, hint);
        area = romCell(l, bank).toAlignedRect();
    }
    for (int bank = 0; bank < ramBanks() && text.isEmpty(); ++bank) {
        if (!ramCell(l, bank).contains(p)) continue;
        text = tips::make(QString("Cartridge RAM bank %1").arg(bank),
            q(glossary("cart.window.ram").body) + (bank == cartridge_.banks.ram ? " This bank is the one selected for $A000–$BFFF." : ""));
        area = ramCell(l, bank).toAlignedRect();
    }
    if (text.isEmpty() && l.rom0Window.contains(p)) { text = tips::key("cart.window.rom0"); area = l.rom0Window.toAlignedRect(); }
    if (text.isEmpty() && l.romWindow.contains(p)) { text = tips::key("cart.window.romx"); area = l.romWindow.toAlignedRect(); }
    if (text.isEmpty() && l.ramWindow.contains(p)) { text = tips::key("cart.window.ram"); area = l.ramWindow.toAlignedRect(); }
    if (text.isEmpty()) text = tips::make("Bank map",
        "Left: the three address windows the CPU uses for the cartridge. Right: every bank on the cartridge. Lines show which "
        "bank each window shows now." + (bootExecutions_ ? QString(" (%1 boot-program instructions are not counted in any bank.)").arg(bootExecutions_) : QString()));
    tips::show(this, help->globalPos(), text, area);
    return true;
}

CartridgePanel::CartridgePanel(QWidget* parent) : QWidget(parent) {
    setObjectName("cartridgePanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    facts_ = new QLabel; facts_->setObjectName("cartridgeFacts"); facts_->setWordWrap(true); facts_->setTextFormat(Qt::RichText);
    facts_->setToolTip(tips::key("cart.facts"));
    facts_->setProperty("informationTopic", "cartridge");
    root->addWidget(facts_);
    notes_ = new QLabel; notes_->setObjectName("cartridgeNotes"); notes_->setWordWrap(true);
    notes_->setProperty("informationTopic", "cartridge");
    notes_->setStyleSheet(QString("color: %1;").arg(style::write.name()));
    root->addWidget(notes_);
    map_ = new BankMap;
    root->addWidget(map_, 3);
    explanation_ = new QLabel; explanation_->setObjectName("cartridgeExplanation"); explanation_->setWordWrap(true);
    root->addWidget(explanation_);
    auto* row = new QHBoxLayout;
    stats_ = new QLabel; stats_->setObjectName("cartridgeStats"); stats_->setWordWrap(true);
    stats_->setProperty("informationTopic", "activity");
    stats_->setToolTip(tips::key("cart.stats"));
    row->addWidget(stats_, 1);
    run_ = new QPushButton("Run until the bank changes");
    run_->setObjectName("runUntilBankChangeButton");
    run_->setToolTip(tips::key("cart.run"));
    row->addWidget(run_);
    root->addLayout(row);
    switches_ = new CompactTable(0, 4);
    switches_->setObjectName("bankSwitchTable");
    switches_->setHorizontalHeaderLabels({"Tick end", "Instruction", "Controller write", "Effect"});
    switches_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    switches_->setSelectionBehavior(QAbstractItemView::SelectRows);
    switches_->verticalHeader()->hide();
    switches_->verticalHeader()->setDefaultSectionSize(22);
    switches_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    switches_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    switches_->setFont(style::monospace());
    switches_->setToolTip(tips::key("cart.switches"));
    switches_->horizontalHeaderItem(0)->setToolTip(tips::key("writes.tick"));
    switches_->horizontalHeaderItem(1)->setToolTip(tips::key("writes.instruction"));
    switches_->horizontalHeaderItem(2)->setToolTip(tips::make("Controller write",
        "The ROM address written and the value. The address range selects which controller register receives it."));
    switches_->horizontalHeaderItem(3)->setToolTip(tips::make("Effect", "What changed in the bank mapping as a result."));
    root->addWidget(switches_, 2);
    connect(switches_, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto* item = switches_->item(row, 2); if (!item || !item->data(Qt::UserRole).isValid()) return;
        emit addressActivated(std::uint16_t(item->data(Qt::UserRole).toUInt()));
    });
    connect(run_, &QPushButton::clicked, this, &CartridgePanel::runUntilBankChange);
    connect(map_, &BankMap::windowActivated, this, &CartridgePanel::addressActivated);
}
void CartridgePanel::setSnapshot(const Snapshot& s, const ActivityMap& activity) {
    const auto& c = s.cartridge;
    const auto& info = c.info;
    QString title = info.title.empty() ? QString("(untitled)") : q(info.title).toHtmlEscaped();
    facts_->setText(QString("<b>%1</b> · %2 · %3 ROM (%4 banks of 16 KiB)%5%6 · header checksum %7")
        .arg(title, q(info.typeName).toHtmlEscaped(), kib(info.fileBytes)).arg(info.romBanks())
        .arg(c.ramBytes ? QString(" · %1 RAM").arg(kib(c.ramBytes)) : QString())
        .arg(info.battery ? " · battery" : "")
        .arg(info.headerChecksumValid ? "OK" : "does not match"));
    QStringList notes;
    if (!info.note.empty()) notes << q(info.note);
    if (info.type != info.effectiveType) notes << QString("Declared header type %1; effective controller type %2 (%3).")
        .arg(q(hex(info.type, 2)), q(hex(info.effectiveType, 2)), q(info.typeName));
    if (info.cgbOnly()) notes << "Marked Game Boy Color only. This lab emulates the original Game Boy, so the game sees a monochrome system; most such games show their own \"requires Game Boy Color\" screen.";
    else if (info.cgbEnhanced()) notes << "Also supports Game Boy Color; running in monochrome Game Boy mode.";
    if (info.sgbFlag == 0x03) notes << "Has Super Game Boy features, which are not emulated here.";
    if (info.timer) notes << "Has a real-time clock. While a clock register is selected, the CPU reads the clock at $A000–$BFFF; this view shows RAM storage.";
    if (info.mbc == Mbc::Mbc2) notes << "MBC2 RAM is 512 half-bytes built into the controller: only the low 4 bits of each byte are stored.";
    notes_->setText(notes.join("\n"));
    notes_->setVisible(!notes.isEmpty());

    const auto mbc = q(mbcName(info.mbc));
    QString text;
    if (!info.banked()) {
        text = QString("This %1 cartridge has no memory bank controller (MBC). The CPU's ROM window, $0000–$7FFF, shows it directly: "
                       "bank 0 at $0000–$3FFF and bank 1 at $4000–$7FFF. Nothing switches, and writes to these addresses are ignored.").arg(kib(info.fileBytes));
    } else {
        text = QString("The CPU addresses only 32 KiB of cartridge ROM, but this cartridge holds %1 in %2 banks of 16 KiB. "
                       "Bank %3 stays at $0000–$3FFF; the %4 chip on the cartridge chooses which bank appears at $4000–$7FFF. "
                       "To switch, the program writes a bank number to a ROM address: ROM cannot change, so the %4 takes the write as a command. "
                       "One CPU address can therefore hold different code at different moments.")
                   .arg(kib(info.fileBytes)).arg(info.romBanks()).arg(c.banks.rom0).arg(mbc);
        if (c.ramBytes) {
            text += QString(" Cartridge RAM appears at $A000–$BFFF%1 once the program enables it%2.")
                        .arg(c.ramBytes > 0x2000 ? ", one 8 KiB bank at a time," : "")
                        .arg(info.battery ? "; a battery keeps it, saved here as a .sav file" : "");
        }
    }
    explanation_->setText(text);
    const auto total = std::accumulate(activity.bankExecutions.begin(), activity.bankExecutions.end(), std::uint64_t{});
    const auto fixed = c.banks.rom0 < activity.bankExecutions.size() ? activity.bankExecutions[c.banks.rom0] : 0;
    stats_->setText(QString("Since t=%1: ROM bank changes %2 · RAM bank changes %3 · opcodes from bank %4: %5, other banks: %6%7")
        .arg(activity.startTicks).arg(activity.romBankChanges).arg(activity.ramBankChanges).arg(c.banks.rom0)
        .arg(fixed).arg(total - fixed).arg(activity.bootExecutions ? QString(" · boot: %1").arg(activity.bootExecutions) : QString()));
    run_->setEnabled(info.banked());
    map_->setData(c, activity);

    std::vector<const WriteEvent*> rows;
    for (auto i = s.writes.rbegin(); i != s.writes.rend() && rows.size() < 32; ++i) if (i->address < 0x8000) rows.push_back(&*i);
    if (!s.traceEnabled) {
        switches_->setRowCount(1);
        auto* item = new QTableWidgetItem("Capture writes is off: bank changes are counted above, but the instructions that cause them are not recorded.");
        switches_->setItem(0, 0, item);
        switches_->setSpan(0, 0, 1, 4);
        return;
    }
    switches_->clearSpans();
    switches_->setRowCount(int(rows.size()));
    for (int r = 0; r < int(rows.size()); ++r) {
        const auto& w = *rows[std::size_t(r)];
        const auto name = mbcRegisterName(info.mbc, w.address);
        const QStringList values{QString::number(w.endTicks),
            w.instruction ? q(hex(w.instruction->pc)) + "  " + q(disassemble(*w.instruction)) : QString("no opcode"),
            QString("%1 ← %2%3").arg(q(hex(w.address)), q(hex(w.requested, 2)), name.empty() ? QString() : "  " + q(name)),
            bankEffect(w, info)};
        for (int col = 0; col < 4; ++col) {
            auto* item = switches_->item(r, col);
            if (!item) { item = new QTableWidgetItem; switches_->setItem(r, col, item); }
            item->setText(values[col]);
            item->setData(Qt::UserRole, w.address);
            item->setToolTip(col == 2 && !name.empty() ? tips::make(q(name), QString("%1 was written to %2. ROM can't change, so the %3 chip "
                                 "takes the write as a command.").arg(q(hex(w.requested, 2)), q(hex(w.address)), q(mbcName(info.mbc))), values[3])
                                                       : values[col]);
            item->setForeground(col == 3 && values[3].contains("→") ? style::highlight : style::text);
        }
    }
}
}
