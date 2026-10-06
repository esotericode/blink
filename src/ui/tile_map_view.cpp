#include "ui/tile_map_view.hpp"
#include "ui/style.hpp"
#include "ui/tooltip.hpp"
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

namespace observatory {
TileMapView::TileMapView(QWidget* parent) : QWidget(parent) {
    setObjectName("tileMapView"); tips::setCustom(this);
}
std::uint16_t TileMapView::base() const {
    if (mode_ == 2) return 0x9800;
    if (mode_ == 3) return 0x9C00;
    return video_.lcdc & (mode_ == 1 ? 0x40 : 0x08) ? 0x9C00 : 0x9800;
}
int TileMapView::selectedTile() const { return backgroundTile(video_.vram[std::size_t(selectedAddress() - 0x8000)], video_.lcdc & 0x10); }
void TileMapView::setSnapshot(const VideoState& video) { video_ = video; rebuild(); }
void TileMapView::setMode(int mode) { mode_ = std::clamp(mode, 0, 3); rebuild(); emit cellSelected(selectedAddress(), selectedTile()); }
void TileMapView::rebuild() {
    image_ = QImage(256, 256, QImage::Format_RGB32);
    const auto colors = shadeColors();
    const auto offset = std::size_t(base() - 0x8000);
    for (int i = 0; i < 1024; ++i) {
        const auto pixels = decodeTile(video_.vram, backgroundTile(video_.vram[offset + std::size_t(i)], video_.lcdc & 0x10));
        for (int y = 0; y < 8; ++y) {
            auto* row = reinterpret_cast<QRgb*>(image_.scanLine((i / 32) * 8 + y));
            for (int x = 0; x < 8; ++x) row[(i % 32) * 8 + x] = colors[shade(video_.bgp, pixels[std::size_t(y * 8 + x)])];
        }
    }
    update();
}
QRect TileMapView::mapRect() const {
    const int side = std::max(1, std::min(width() - 16, height() - 16));
    return {(width() - side) / 2, (height() - side) / 2, side, side};
}
int TileMapView::cellAt(QPoint p) const {
    const auto r = mapRect(); if (!r.contains(p)) return -1;
    return std::clamp((p.y() - r.top()) * 32 / r.height(), 0, 31) * 32 + std::clamp((p.x() - r.left()) * 32 / r.width(), 0, 31);
}
void TileMapView::paintEvent(QPaintEvent*) {
    QPainter p(this); p.fillRect(rect(), style::panel);
    const auto r = mapRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform, false); p.drawImage(r, image_);
    const double cell = double(r.width()) / 32;
    p.setPen(QPen(style::highlight, 2));
    p.drawRect(QRectF(r.x() + (selected_ % 32) * cell, r.y() + (selected_ / 32) * cell, cell, cell));
}
void TileMapView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const auto cell = cellAt(event->position().toPoint()); if (cell < 0) return;
    selected_ = cell; update(); emit cellSelected(selectedAddress(), selectedTile());
}
bool TileMapView::event(QEvent* event) {
    if (event->type() != QEvent::ToolTip) return QWidget::event(event);
    auto* help = static_cast<QHelpEvent*>(event); const int cell = cellAt(help->pos());
    if (cell < 0) return true;
    const auto address = std::uint16_t(base() + cell); const auto entry = video_.vram[std::size_t(address - 0x8000)];
    const int tile = backgroundTile(entry, video_.lcdc & 0x10);
    tips::show(this, help->globalPos(), tips::make(QString("Map cell (%1, %2) · %3").arg(cell % 32).arg(cell / 32).arg(style::q(hex(address))),
        QString("Entry %1 currently selects physical tile %2 at %3. This is reconstructed storage, not the completed screen.")
            .arg(style::q(hex(entry, 2))).arg(tile).arg(style::q(hex(tileAddress(tile)))), "Click for detailed map information and the tile's bit planes."), mapRect());
    return true;
}
} // namespace observatory
