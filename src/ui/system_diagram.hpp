#pragma once
#include "emulator/state.hpp"
#include <QWidget>
#include <QLineF>
#include <map>
#include <set>
#include <vector>

namespace observatory {
// Functional schematic of the parts a button press travels through. Live labels
// come from the shared snapshot; it is not a circuit diagram.
class SystemDiagram : public QWidget {
    Q_OBJECT
public:
    enum class Block { Joypad, Cartridge, Cpu, Wram, Vram, Oam, Ppu, Lcd };
    enum class Path { JoypadCpu, RomCpu, CpuWram, CpuOam, CpuVram, VramPpu, OamPpu, PpuLcd, WramOamDma };
    explicit SystemDiagram(QWidget* parent = nullptr);
    void setSnapshot(const Snapshot& snapshot);
    void setHighlight(std::set<Path> paths, std::set<Block> blocks);
    const std::set<Path>& highlightedPaths() const { return paths_; }
    QSize minimumSizeHint() const override { return {420, 150}; }
    QSize sizeHint() const override { return {640, 170}; }
signals:
    void blockActivated(observatory::SystemDiagram::Block block);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;
private:
    std::map<Block, QRectF> layout() const;
    // Straight arrows, shared by painting and hover hit-testing.
    std::vector<std::pair<Path, QLineF>> arrows(const std::map<Block, QRectF>& r) const;
    QRectF dmaArea(const std::map<Block, QRectF>& r) const;
    std::map<Block, QString> detail_;
    std::map<Path, QString> traffic_;
    std::set<Path> paths_;
    std::set<Block> blocks_;
};
}
