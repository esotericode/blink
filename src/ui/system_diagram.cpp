#include "ui/system_diagram.hpp"
#include "teaching_rom.hpp"
#include "ui/style.hpp"
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
const char* titles[] = {"Joypad", "Cartridge ROM", "CPU · SM83", "WRAM $C000", "VRAM $8000", "OAM $FE00", "PPU", "LCD"};
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
    setToolTip("Functional schematic: click a part to inspect its storage.");
}
void SystemDiagram::setSnapshot(const Snapshot& s) {
    detail_[Block::Joypad] = "JOYP $FF00\n" + held(s.heldButtons);
    detail_[Block::Cartridge] = "$0000–$7FFF\n32 KiB, ROM only";
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
    auto mid = [](const QRectF& box, double y) { return std::clamp(y, box.top() + 8, box.bottom() - 8); };
    const auto& cpu = r.at(Block::Cpu);
    const auto& ppu = r.at(Block::Ppu);
    for (auto [block, path] : {std::pair{Block::Joypad, Path::JoypadCpu}, std::pair{Block::Cartridge, Path::RomCpu}}) {
        const auto& box = r.at(block);
        const double y = mid(cpu, box.center().y());
        arrow(QPointF(box.right() + 2, y), QPointF(cpu.left() - 2, y), path, {});
    }
    for (auto [block, path] : {std::pair{Block::Wram, Path::CpuWram}, std::pair{Block::Oam, Path::CpuOam}, std::pair{Block::Vram, Path::CpuVram}}) {
        const auto& box = r.at(block);
        const double y = box.center().y();
        arrow(QPointF(cpu.right() + 2, mid(cpu, y)), QPointF(box.left() - 2, y), path, traffic_[path]);
    }
    for (auto [block, path] : {std::pair{Block::Oam, Path::OamPpu}, std::pair{Block::Vram, Path::VramPpu}}) {
        const auto& box = r.at(block);
        arrow(QPointF(box.right() + 2, box.center().y()), QPointF(ppu.left() - 2, mid(ppu, box.center().y())), path, {});
    }
    arrow(QPointF(ppu.right() + 2, ppu.center().y()), QPointF(r.at(Block::Lcd).left() - 2, ppu.center().y()), Path::PpuLcd, {});

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
    painter.setFont(font);
    painter.setPen(style::muted);
    painter.drawText(QRectF(6, height() - 15, width() - 12, 14), Qt::AlignLeft | Qt::AlignVCenter,
                     "Functional schematic, not circuitry · arrow numbers: CPU write attempts in the last published interval");
}
void SystemDiagram::mousePressEvent(QMouseEvent* event) {
    for (const auto& [block, box] : layout()) {
        if (box.contains(event->position())) { emit blockActivated(block); return; }
    }
}
}
