#pragma once
#include "emulator/state.hpp"
#include <QImage>
#include <QWidget>

namespace observatory {
class GameView : public QWidget {
    Q_OBJECT
public:
    explicit GameView(QWidget* parent = nullptr);
    void setFrame(const Snapshot& snapshot);
    QSize sizeHint() const override { return {480, 432}; }
    const QImage& image() const { return image_; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QImage image_;
};
}
