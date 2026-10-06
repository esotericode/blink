#pragma once
#include "emulator/state.hpp"
#include <QWidget>
#include <set>

class QButtonGroup;
class QComboBox;
class QLabel;
class QTableWidget;

namespace observatory {
// How tile pixels are coloured: through a palette register, or as raw colour numbers.
enum class TilePalette { Background, Object0, Object1, ColorNumbers };

// One 128-tile block of $8000-$97FF ($8000, $8800, or $9000), decoded from copied VRAM.
class TileSheet : public QWidget {
    Q_OBJECT
public:
    explicit TileSheet(QWidget* parent = nullptr);
    void setData(const VideoState& video, TilePalette palette, std::set<int> spriteTiles, std::set<int> mapTiles);
    void setSelected(int tile);
    void setBlock(int block);
    int selected() const { return selected_; }
    int block() const { return block_; }
    QSize sizeHint() const override { return {16 * 17 + 1, 8 * 17 + 1}; }
    QSize minimumSizeHint() const override { return {16 * 9 + 1, 8 * 9 + 1}; }
signals:
    void tileClicked(int tile);
    void blockChanged(int block);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;
private:
    int scale() const;
    int tileAt(QPoint point) const;
    QImage image_;
    std::set<int> spriteTiles_, mapTiles_;
    int selected_ = 2, block_ = 0;
};

// One tile's two bit planes, the colour numbers they form, and the palette mapping.
class TileDetail : public QWidget {
    Q_OBJECT
public:
    explicit TileDetail(QWidget* parent = nullptr);
    void setData(const VideoState& video, int tile, TilePalette palette, QString usage);
    QSize minimumSizeHint() const override { return {380, 250}; }
    QSize sizeHint() const override { return {440, 290}; }
signals:
    void addressActivated(std::uint16_t address);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;
private:
    struct Layout { int bit, pixel, rowsTop, rowHeight, lowHexX, lowX, highHexX, highX, pixelX; };
    Layout layout() const;
    VideoState video_;
    int tile_ = 2;
    TilePalette palette_ = TilePalette::Object0;
    QString usage_;
};

class TileInspector : public QWidget {
    Q_OBJECT
public:
    explicit TileInspector(QWidget* parent = nullptr);
    void setSnapshot(const Snapshot& snapshot);
    void resetSelection();
    void selectSprite(int index);
    void selectTile(int tile);
    int selectedSprite() const { return sprite_; }
    int selectedTile() const;
signals:
    void spriteSelected(int index);
    void addressActivated(std::uint16_t address);
private:
    TilePalette palette() const;
    void updateViews();
    QTableWidget* oam_{};
    QComboBox* paletteChoice_{};
    QButtonGroup* blocks_{};
    TileSheet* sheet_{};
    TileDetail* detail_{};
    QLabel *caption_{}, *oamSource_{};
    VideoState video_;
    int sprite_ = -1;
    bool haveData_ = false;
};
}
