#include "ui/game_view.hpp"
#include "ui/style.hpp"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace observatory {
GameView::GameView(QWidget* parent) : QWidget(parent) {
    setObjectName("gameView");
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(160, 144);
    setToolTip("Arrow keys move the star. F5 runs or pauses; F10 steps an instruction; F11 advances to the next completed output. Click a sprite outline to inspect it.");
}
void GameView::setFrame(const Snapshot& snapshot) {
    image_ = QImage(reinterpret_cast<const uchar*>(snapshot.pixels.data()), 160, 144,
                    160 * sizeof(std::uint32_t), QImage::Format_RGB32).copy();
    changes_ = QImage(160, 144, QImage::Format_ARGB32_Premultiplied);
    changes_.fill(Qt::transparent);
    changed_ = 0;
    if (snapshot.previousFrame) {
        const auto mark = style::highlight.rgba();
        for (int i = 0; i < int(screenPixels); ++i) {
            if (snapshot.pixels[i] != snapshot.previousPixels[i]) {
                changes_.setPixel(i % 160, i / 160, mark);
                ++changed_;
            }
        }
    }
    spriteHeight_ = snapshot.video.lcdc & 0x04 ? 16 : 8;
    sprites_.clear();
    for (int i = 0; i < spriteCount; ++i) {
        auto s = sprite(snapshot.video.oam, i);
        if (s.onScreen(spriteHeight_)) sprites_.push_back(s);
    }
    update();
}
void GameView::setShowSprites(bool on) { showSprites_ = on; update(); }
void GameView::setShowChanges(bool on) { showChanges_ = on; update(); }
void GameView::setSelectedSprite(int index) { selected_ = index; update(); }
QRectF GameView::screenRect() const {
    // Scale by whole *device* pixels so each Game Boy pixel stays square and sharp
    // under fractional desktop scaling (e.g. Windows at 125% or 150%).
    const double dpr = devicePixelRatioF();
    const double available = std::min(width() * dpr / 160.0, height() * dpr / 144.0);
    const double scale = available >= 1 ? std::floor(available) : available;
    const double w = 160 * scale, h = 144 * scale;
    return {std::round((width() * dpr - w) / 2) / dpr, std::round((height() * dpr - h) / 2) / dpr, w / dpr, h / dpr};
}
void GameView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#0b1116"));
    if (image_.isNull()) return;
    const auto target = screenRect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(target, image_);
    const double unit = target.width() / 160.0;
    if (showChanges_ && changed_) {
        painter.setOpacity(0.75);
        painter.drawImage(target, changes_);
        painter.setOpacity(1);
    }
    if (showSprites_ || selected_ >= 0) {
        painter.setClipRect(target);
        painter.setRenderHint(QPainter::Antialiasing, false);
        for (const auto& s : sprites_) {
            const bool chosen = s.index == selected_;
            if (!showSprites_ && !chosen) continue;
            const QRectF box(target.x() + s.screenX() * unit, target.y() + s.screenY() * unit, 8 * unit, spriteHeight_ * unit);
            QPen pen(chosen ? style::highlight : style::accent, chosen ? 3 : 2, chosen ? Qt::SolidLine : Qt::DashLine);
            painter.setPen(pen);
            painter.drawRect(box.adjusted(-2, -2, 2, 2));
            painter.setPen(chosen ? style::highlight : style::accent);
            painter.drawText(box.topLeft() + QPointF(0, -5), QString("#%1").arg(s.index));
        }
    }
}
void GameView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    const auto target = screenRect();
    const double unit = target.width() / 160.0;
    const auto p = event->position();
    const double x = (p.x() - target.x()) / unit, y = (p.y() - target.y()) / unit;
    for (const auto& s : sprites_) {
        if (x >= s.screenX() - 1 && x < s.screenX() + 9 && y >= s.screenY() - 1 && y < s.screenY() + spriteHeight_ + 1) {
            emit spriteClicked(s.index);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}
}
