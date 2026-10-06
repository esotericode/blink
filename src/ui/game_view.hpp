#pragma once
#include "emulator/state.hpp"
#include <QImage>
#include <QWidget>
#include <vector>

namespace observatory {
// Shows the latest completed output. Optional overlays are labelled by their source:
// sprite outlines come from OAM at the CPU cursor (which may be newer than the
// picture), and change marks compare the two most recent completed outputs.
class GameView : public QWidget {
    Q_OBJECT
public:
    explicit GameView(QWidget* parent = nullptr);
    void setFrame(const Snapshot& snapshot);
    void setShowSprites(bool on);
    void setShowChanges(bool on);
    void setSelectedSprite(int index);
    bool showSprites() const { return showSprites_; }
    bool showChanges() const { return showChanges_; }
    int changedPixels() const { return changed_; }
    int selectedSprite() const { return selected_; }
    QSize sizeHint() const override { return {480, 432}; }
    const QImage& image() const { return image_; }
signals:
    void spriteClicked(int index);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;
private:
    QRectF screenRect() const;
    const Sprite* spriteAt(QPointF point) const;
    QImage image_, changes_;
    std::vector<Sprite> sprites_;
    int spriteHeight_ = 8, selected_ = -1, changed_ = 0;
    bool showSprites_ = false, showChanges_ = false;
};
}
