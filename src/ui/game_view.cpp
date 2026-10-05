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
    const double available = std::min(width() / 160.0, height() / 144.0);
    const double scale = available >= 1 ? std::floor(available) : available;
    const QSize size(int(160 * scale), int(144 * scale));
    const QRect target((width()-size.width())/2, (height()-size.height())/2, size.width(), size.height());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(target, image_);
}
}
