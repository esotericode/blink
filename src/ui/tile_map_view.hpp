#pragma once
#include "emulator/state.hpp"
#include <QWidget>

namespace observatory {
// A reconstruction of current map storage, not historical screen-pixel input.
class TileMapView : public QWidget {
    Q_OBJECT
public:
    explicit TileMapView(QWidget* parent = nullptr);
    void setSnapshot(const VideoState& video);
    void setMode(int mode); // 0 background, 1 window, 2 $9800, 3 $9C00
    std::uint16_t base() const;
    std::uint16_t selectedAddress() const { return std::uint16_t(base() + selected_); }
    int selectedTile() const;
    QSize minimumSizeHint() const override { return {240, 240}; }
    QSize sizeHint() const override { return {320, 320}; }
signals:
    void cellSelected(std::uint16_t address, int tile);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;
private:
    QRect mapRect() const;
    int cellAt(QPoint point) const;
    void rebuild();
    VideoState video_;
    QImage image_;
    int mode_ = 0, selected_ = 0;
};
} // namespace observatory
