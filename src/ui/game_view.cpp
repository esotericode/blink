#include "ui/game_view.hpp"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace observatory {
GameView::GameView(QWidget* parent) : QWidget(parent) {
    setObjectName("gameView");
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 288);
    setToolTip("Arrow keys move the star. F5 runs or pauses; F10 steps an instruction; F11 advances to the next completed output.");
}
void GameView::setFrame(const Snapshot& snapshot) {
    image_ = QImage(reinterpret_cast<const uchar*>(snapshot.pixels.data()), 160, 144,
                    160 * sizeof(std::uint32_t), QImage::Format_RGB32).copy();
    update();
}
void GameView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#10171d"));
    if (image_.isNull()) return;
    // Scale by whole *device* pixels so each Game Boy pixel stays square and sharp
    // under fractional desktop scaling (e.g. Windows at 125% or 150%).
    const double dpr = devicePixelRatioF();
    const double available = std::min(width() * dpr / 160.0, height() * dpr / 144.0);
    const double scale = available >= 1 ? std::floor(available) : available;
    const double w = 160 * scale, h = 144 * scale;
    const QRectF target(std::round((width() * dpr - w) / 2) / dpr, std::round((height() * dpr - h) / 2) / dpr, w / dpr, h / dpr);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(target, image_);
}
}
