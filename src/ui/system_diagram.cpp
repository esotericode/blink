#include "ui/system_diagram.hpp"
#include "teaching_rom.hpp"
#include "ui/style.hpp"
#include "ui/tooltip.hpp"
#include <QHelpEvent>
#include <QMouseEvent>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace observatory {
namespace {
using style::q;
using Block = SystemDiagram::Block;
using Path = SystemDiagram::Path;
const char* titles[] = {"Joypad", "Cartridge", "CPU · SM83", "WRAM $C000", "VRAM $8000", "OAM $FE00", "PPU", "LCD"};
QString held(std::uint8_t mask) {
    static const char* names[] = {"Right", "Left", "Up", "Down", "A", "B", "Select", "Start"};
    QStringList out;
    for (int i = 0; i < 8; ++i) if (mask & (1 << i)) out << names[i];
    return out.isEmpty() ? "no button held" : "held: " + out.join("+");
}
}

SystemDiagram::SystemDiagram(QWidget* parent) : QWidget(parent) {
    setObjectName("systemDiagram");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tips::setCustom(this);
}
void SystemDiagram::setSnapshot(const Snapshot& s) {
    detail_[Block::Joypad] = "JOYP $FF00\n" + held(s.heldButtons);
    const auto& c = s.cartridge;
    detail_[Block::Cartridge] = c.info.banked()
        ? QString("%1 · %2 banks\nbank %3 at $4000").arg(q(mbcName(c.info.mbc))).arg(c.info.romBanks()).arg(c.banks.rom)
        : QString("no MBC · 32 KiB\nfixed banks 0 and 1");
    detail_[Block::Cpu] = QString("PC %1 · A %2\n%3").arg(q(hex(s.registers.pc)), q(hex(s.registers.af >> 8, 2)),
        s.lastExecuted ? q(disassemble(*s.lastExecuted)) : QString("no opcode yet"));
    detail_[Block::Wram] = s.teaching ? QString("player_x = %1").arg(s.playerX) : QString("8 KiB work RAM");
    detail_[Block::Vram] = "tile data + maps";
    detail_[Block::Oam] = QString("sprite 0 X+8 = %1").arg(s.video.oam[1]);
    detail_[Block::Ppu] = QString("LY %1\nLCDC %2\nreads OAM + VRAM").arg(s.video.ly).arg(q(hex(s.video.lcdc, 2)));
    detail_[Block::Lcd] = QString("160×144\noutput #%1").arg(s.frames);
    const auto& w = s.activity.writes;
    auto count = [](std::uint64_t n) { return n ? QString::number(n) : QString(); };
    traffic_[Path::CpuWram] = count(w[2]);
    traffic_[Path::CpuOam] = count(w[3]);
    traffic_[Path::CpuVram] = count(w[1]);
    traffic_[Path::WramOamDma] = s.activity.dmaTransfers ? QString("DMA %1").arg(s.activity.dmaTransfers) : QString("DMA");
    update();
}
void SystemDiagram::setHighlight(std::set<Path> paths, std::set<Block> blocks) {
    paths_ = std::move(paths); blocks_ = std::move(blocks);
    update();
}
std::map<Block, QRectF> SystemDiagram::layout() const {
    const double margin = 6, gap = 34, footer = 16;
    const double w = (width() - 2 * margin - 4 * gap) / 5, h = height() - 2 * margin - footer;
    auto column = [&](int i) { return margin + i * (w + gap); };
    std::map<Block, QRectF> r;
    const double half = (h - 8) / 2, third = (h - 12) / 3;
    r[Block::Joypad] = QRectF(column(0), margin, w, half);
    r[Block::Cartridge] = QRectF(column(0), margin + half + 8, w, half);
    r[Block::Cpu] = QRectF(column(1), margin + h * 0.12, w, h * 0.76);
    r[Block::Wram] = QRectF(column(2), margin, w, third);
    r[Block::Oam] = QRectF(column(2), margin + third + 6, w, third);
    r[Block::Vram] = QRectF(column(2), margin + 2 * (third + 6), w, third);
    r[Block::Ppu] = QRectF(column(3), margin + h * 0.2, w, h * 0.6);
    r[Block::Lcd] = QRectF(column(4), margin + h * 0.2, w, h * 0.6);
    return r;
}
void SystemDiagram::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), style::window);
    const auto r = layout();
    auto font = this->font();
    font.setPointSizeF(std::max(7.0, font.pointSizeF() - 1.5));
    auto arrow = [&](QPointF from, QPointF to, Path path, const QString& label) {
        const bool on = paths_.count(path);
        const QColor colour = on ? style::highlight : style::muted;
        painter.setPen(QPen(colour, on ? 3 : 1.4));
        painter.drawLine(from, to);
        const double angle = std::atan2(to.y() - from.y(), to.x() - from.x());
        const QPointF a = to - QPointF(std::cos(angle - 0.45) * 8, std::sin(angle - 0.45) * 8);
        const QPointF b = to - QPointF(std::cos(angle + 0.45) * 8, std::sin(angle + 0.45) * 8);
        painter.setBrush(colour); painter.setPen(Qt::NoPen);
        painter.drawPolygon(QPolygonF({to, a, b}));
        if (!label.isEmpty()) {
            painter.setFont(font);
            painter.setPen(on ? style::highlight : style::write);
            painter.drawText(QRectF((from.x() + to.x()) / 2 - 18, (from.y() + to.y()) / 2 - 15, 36, 13), Qt::AlignCenter, label);
        }
    };
    for (const auto& [path, line] : arrows(r)) {
        const bool counted = path == Path::CpuWram || path == Path::CpuOam || path == Path::CpuVram;
        arrow(line.p1(), line.p2(), path, counted ? traffic_[path] : QString());
    }
    {
        // OAM DMA: a hardware copy (usually from a WRAM "shadow" table) that bypasses the CPU.
        const auto& wram = r.at(Block::Wram);
        const auto& oam = r.at(Block::Oam);
        const bool lit = paths_.count(Path::WramOamDma), active = traffic_[Path::WramOamDma] != "DMA";
        const QColor colour = lit ? style::highlight : active ? style::write : style::muted;
        const QPointF from(wram.right() + 2, wram.bottom() - 7), to(oam.right() + 2, oam.top() + 7);
        QPainterPath curve(from);
        curve.cubicTo(from + QPointF(16, 0), to + QPointF(16, 0), to + QPointF(5, 0));
        painter.setPen(QPen(colour, lit ? 3 : 1.4, active || lit ? Qt::SolidLine : Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(curve);
        painter.setPen(Qt::NoPen); painter.setBrush(colour);
        painter.drawPolygon(QPolygonF({to, to + QPointF(7, -3.5), to + QPointF(7, 3.5)}));
    }
    for (const auto& [block, box] : r) {
        const bool on = blocks_.count(block);
        painter.setPen(QPen(on ? style::highlight : style::border, on ? 2.5 : 1));
        painter.setBrush(on ? QColor("#2a2030") : style::panel);
        painter.drawRoundedRect(box, 5, 5);
        auto bold = this->font(); bold.setBold(true); bold.setPointSizeF(std::max(7.5, bold.pointSizeF() - 1));
        painter.setFont(bold);
        painter.setPen(on ? style::highlight : style::accent);
        painter.drawText(box.adjusted(6, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop, titles[int(block)]);
        painter.setFont(font);
        painter.setPen(style::text);
        // One line per fact, elided rather than wrapped so narrow layouts stay legible.
        const QFontMetricsF metrics(font);
        double y = box.top() + 18;
        for (const auto& line : detail_[block].split('\n')) {
            if (y + metrics.height() > box.bottom() + 1) break;
            painter.drawText(QPointF(box.left() + 6, y + metrics.ascent()), metrics.elidedText(line, Qt::ElideRight, box.width() - 10));
            y += metrics.height();
        }
    }
    // DMA copies in the interval, in the OAM box's title row (the gap beside the boxes is too narrow).
    if (traffic_[Path::WramOamDma] != "DMA" || paths_.count(Path::WramOamDma)) {
        painter.setFont(font);
        painter.setPen(paths_.count(Path::WramOamDma) ? style::highlight : style::write);
        painter.drawText(r.at(Block::Oam).adjusted(4, 4, -6, 0), Qt::AlignRight | Qt::AlignTop, traffic_[Path::WramOamDma]);
    }
    painter.setFont(font);
    painter.setPen(style::muted);
    painter.drawText(QRectF(6, height() - 15, width() - 12, 14), Qt::AlignLeft | Qt::AlignVCenter,
                     "Functional schematic, not circuitry · arrow numbers: CPU write attempts in the last published interval");
}
std::vector<std::pair<SystemDiagram::Path, QLineF>> SystemDiagram::arrows(const std::map<Block, QRectF>& r) const {
    auto mid = [](const QRectF& box, double y) { return std::clamp(y, box.top() + 8, box.bottom() - 8); };
    const auto& cpu = r.at(Block::Cpu);
    const auto& ppu = r.at(Block::Ppu);
    std::vector<std::pair<Path, QLineF>> out;
    for (auto [block, path] : {std::pair{Block::Joypad, Path::JoypadCpu}, std::pair{Block::Cartridge, Path::RomCpu}}) {
        const auto& box = r.at(block);
        const double y = mid(cpu, box.center().y());
        out.push_back({path, QLineF(box.right() + 2, y, cpu.left() - 2, y)});
    }
    for (auto [block, path] : {std::pair{Block::Wram, Path::CpuWram}, std::pair{Block::Oam, Path::CpuOam}, std::pair{Block::Vram, Path::CpuVram}}) {
        const auto& box = r.at(block);
        out.push_back({path, QLineF(cpu.right() + 2, mid(cpu, box.center().y()), box.left() - 2, box.center().y())});
    }
    for (auto [block, path] : {std::pair{Block::Oam, Path::OamPpu}, std::pair{Block::Vram, Path::VramPpu}}) {
        const auto& box = r.at(block);
        out.push_back({path, QLineF(box.right() + 2, box.center().y(), ppu.left() - 2, mid(ppu, box.center().y()))});
    }
    out.push_back({Path::PpuLcd, QLineF(ppu.right() + 2, ppu.center().y(), r.at(Block::Lcd).left() - 2, ppu.center().y())});
    return out;
}
QRectF SystemDiagram::dmaArea(const std::map<Block, QRectF>& r) const {
    const auto& wram = r.at(Block::Wram);
    const auto& oam = r.at(Block::Oam);
    return QRectF(QPointF(wram.right(), wram.bottom() - 12), QPointF(wram.right() + 58, oam.top() + 12));
}
bool SystemDiagram::event(QEvent* event) {
    if (event->type() != QEvent::ToolTip) return QWidget::event(event);
    auto* help = static_cast<QHelpEvent*>(event);
    const QPointF p = help->pos();
    const auto r = layout();
    static const std::map<Block, const char*> parts{{Block::Joypad, "part.joypad"}, {Block::Cartridge, "part.cartridge"},
        {Block::Cpu, "part.cpu"}, {Block::Wram, "part.wram"}, {Block::Vram, "part.vram"}, {Block::Oam, "part.oam"},
        {Block::Ppu, "part.ppu"}, {Block::Lcd, "part.lcd"}};
    for (const auto& [block, box] : r) {
        if (box.contains(p)) { tips::show(this, help->globalPos(), tips::key(parts.at(block)), box.toAlignedRect()); return true; }
    }
    if (const auto area = dmaArea(r); area.contains(p)) {
        tips::show(this, help->globalPos(), tips::key("path.dma"), area.toAlignedRect());
        return true;
    }
    for (const auto& [path, line] : arrows(r)) {
        // Distance from the pointer to the arrow's segment.
        const QPointF d = line.p2() - line.p1();
        const double t = std::clamp(QPointF::dotProduct(p - line.p1(), d) / std::max(1.0, QPointF::dotProduct(d, d)), 0.0, 1.0);
        const QPointF nearest = line.p1() + t * d;
        if (QLineF(p, nearest).length() > 7) continue;
        const char* key = path == Path::JoypadCpu ? "path.joypad" : path == Path::RomCpu ? "path.rom"
                        : path == Path::PpuLcd ? "path.lcd" : (path == Path::OamPpu || path == Path::VramPpu) ? "path.read" : "path.write";
        tips::show(this, help->globalPos(), tips::key(key), QRectF(line.p1(), line.p2()).normalized().adjusted(-8, -8, 8, 8).toAlignedRect());
        return true;
    }
    tips::show(this, help->globalPos(), tips::key("panel.system"), rect());
    return true;
}
void SystemDiagram::mousePressEvent(QMouseEvent* event) {
    for (const auto& [block, box] : layout()) {
        if (box.contains(event->position())) { emit blockActivated(block); return; }
    }
}
}
