#include "ui/tile_view.hpp"
#include "ui/style.hpp"
#include <QButtonGroup>
#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>

namespace observatory {
namespace {
using style::q;
// Colour numbers without a palette: a neutral ramp, so they are not mistaken for shades.
const QColor rawRamp[4] = {QColor("#e9edf0"), QColor("#a9b4bc"), QColor("#66737d"), QColor("#26303a")};
const QColor transparentTile("#2a3a46");

std::uint8_t paletteRegister(const VideoState& v, TilePalette p) {
    return p == TilePalette::Object0 ? v.obp0 : p == TilePalette::Object1 ? v.obp1 : v.bgp;
}
bool isObject(TilePalette p) { return p == TilePalette::Object0 || p == TilePalette::Object1; }
QColor indexColor(const VideoState& v, TilePalette p, std::uint8_t index) {
    static const auto shades = shadeColors();
    if (p == TilePalette::ColorNumbers) return rawRamp[index];
    if (isObject(p) && index == 0) return transparentTile;
    return QColor::fromRgba(shades[shade(paletteRegister(v, p), index)]);
}
QString paletteName(TilePalette p) {
    switch (p) {
    case TilePalette::Background: return "BGP ($FF47)";
    case TilePalette::Object0: return "OBP0 ($FF48)";
    case TilePalette::Object1: return "OBP1 ($FF49)";
    default: return "no palette";
    }
}
QColor readable(const QColor& background) { return background.lightness() > 120 ? QColor("#10171d") : QColor("#f4f7f9"); }
QString tileRange(int tile) {
    return QString("%1–%2").arg(q(hex(tileAddress(tile))), q(hex(tileAddress(tile) + 15)));
}
}

TileSheet::TileSheet(QWidget* parent) : QWidget(parent) {
    setObjectName("tileSheet");
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void TileSheet::setData(const VideoState& video, TilePalette palette, std::set<int> spriteTiles, std::set<int> mapTiles) {
    image_ = QImage(128, 192, QImage::Format_RGB32);
    for (int t = 0; t < tileCount; ++t) {
        const auto pixels = decodeTile(video.vram, t);
        for (int i = 0; i < 64; ++i) {
            image_.setPixel((t % 16) * 8 + i % 8, (t / 16) * 8 + i / 8, indexColor(video, palette, pixels[i]).rgb());
        }
    }
    spriteTiles_ = std::move(spriteTiles);
    mapTiles_ = std::move(mapTiles);
    update();
}
void TileSheet::setSelected(int tile) {
    selected_ = tile;
    setBlock(tile / 128);
    update();
}
void TileSheet::setBlock(int block) {
    block = std::clamp(block, 0, 2);
    if (block == block_) return;
    block_ = block;
    emit blockChanged(block);
    update();
}
int TileSheet::scale() const {
    const int byWidth = ((width() - 1) / 16 - 1) / 8, byHeight = ((height() - 1) / 8 - 1) / 8;
    return std::max(1, std::min(byWidth, byHeight));
}
int TileSheet::tileAt(QPoint point) const {
    const int cell = 8 * scale() + 1;
    const int column = (point.x() - 1) / cell, row = (point.y() - 1) / cell;
    if (point.x() < 1 || point.y() < 1 || column >= 16 || row >= 8) return -1;
    return block_ * 128 + row * 16 + column;
}
void TileSheet::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), style::window);
    if (image_.isNull()) return;
    const int s = scale(), cell = 8 * s + 1;
    painter.fillRect(QRect(0, 0, 16 * cell + 1, 8 * cell + 1), style::border);
    for (int t = block_ * 128; t < block_ * 128 + 128; ++t) {
        const int slot = t - block_ * 128;
        const QRect target(1 + (slot % 16) * cell, 1 + (slot / 16) * cell, 8 * s, 8 * s);
        painter.drawImage(target, image_, QRect((t % 16) * 8, (t / 16) * 8, 8, 8));
        if (mapTiles_.count(t)) {
            painter.setPen(Qt::NoPen); painter.setBrush(style::muted);
            const int m = std::max(3, s * 2);
            painter.drawPolygon(QPolygon({target.topLeft(), target.topLeft() + QPoint(m, 0), target.topLeft() + QPoint(0, m)}));
        }
        if (spriteTiles_.count(t)) {
            painter.setBrush(Qt::NoBrush); painter.setPen(QPen(style::accent, 1));
            painter.drawRect(target.adjusted(0, 0, -1, -1));
        }
    }
    if (selected_ / 128 == block_) {
        const int slot = selected_ - block_ * 128;
        painter.setBrush(Qt::NoBrush); painter.setPen(QPen(style::highlight, 2));
        painter.drawRect(QRect(1 + (slot % 16) * cell, 1 + (slot / 16) * cell, 8 * s, 8 * s).adjusted(-1, -1, 1, 1));
    }
}
void TileSheet::mousePressEvent(QMouseEvent* event) {
    const int t = tileAt(event->position().toPoint());
    if (t >= 0) emit tileClicked(t);
}
bool TileSheet::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const int t = tileAt(help->pos());
        if (t < 0) { QToolTip::hideText(); return true; }
        QString text = QString("Tile %1 · %2").arg(t).arg(tileRange(t));
        if (t < 128) text += "\nBlock $8000: sprites, and background when LCDC bit 4 = 1";
        else if (t < 256) text += "\nBlock $8800: shared by sprites and background";
        else text += "\nBlock $9000: background when LCDC bit 4 = 0";
        if (spriteTiles_.count(t)) text += "\nUsed by an on-screen sprite";
        if (mapTiles_.count(t)) text += "\nReferenced by the background map";
        QToolTip::showText(help->globalPos(), text, this);
        return true;
    }
    return QWidget::event(event);
}

TileDetail::TileDetail(QWidget* parent) : QWidget(parent) {
    setObjectName("tileDetail");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip("Click a row to show its two bytes in the memory inspector.");
}
void TileDetail::setData(const VideoState& video, int tile, TilePalette palette, QString usage) {
    video_ = video; tile_ = tile; palette_ = palette; usage_ = std::move(usage);
    update();
}
TileDetail::Layout TileDetail::layout() const {
    // Each row: address | low byte + 8 bits | high byte + 8 bits | 8 magnified pixels.
    // 16 bit cells + 8 pixel cells (pixel = bit + 5) share the width left after labels.
    Layout l{};
    const int fixed = 8 + 50 + 28 + 10 + 28 + 14 + 8;
    l.bit = std::clamp((width() - fixed - 40) / 24, 8, 16);
    l.pixel = l.bit + 5;
    l.rowsTop = 82;
    l.rowHeight = l.pixel + 1;
    l.lowHexX = 8 + 50;
    l.lowX = l.lowHexX + 28;
    l.highHexX = l.lowX + 8 * l.bit + 10;
    l.highX = l.highHexX + 28;
    l.pixelX = l.highX + 8 * l.bit + 14;
    return l;
}
void TileDetail::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), style::window);
    const auto l = layout();
    const auto pixels = decodeTile(video_.vram, tile_);
    auto font = style::monospace();
    font.setPointSizeF(std::max(7.0, font.pointSizeF() - 1));
    const QFontMetrics metrics(font);
    auto bold = font; bold.setBold(true);
    painter.setFont(bold);
    painter.setPen(style::accent);
    painter.drawText(QRect(8, 2, width() - 16, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(QString("Tile %1 · %2 · %3").arg(tile_).arg(tileRange(tile_), usage_), Qt::ElideRight, width() - 16));
    painter.setFont(font);

    // Palette: the register's four 2-bit fields, then what each colour number becomes.
    painter.setPen(style::text);
    if (palette_ == TilePalette::ColorNumbers) {
        painter.drawText(QRect(8, 22, width() - 16, 18), Qt::AlignVCenter, "Raw colour numbers 0–3 (no palette applied)");
    } else {
        const auto reg = paletteRegister(video_, palette_);
        painter.drawText(QRect(8, 22, width() - 16, 18), Qt::AlignVCenter,
            metrics.elidedText(QString("%1 = %2 = %3 %4 %5 %6 (fields for colour 3, 2, 1, 0)").arg(paletteName(palette_), q(hex(reg, 2)))
                .arg((reg >> 6) & 3, 2, 2, QChar('0')).arg((reg >> 4) & 3, 2, 2, QChar('0'))
                .arg((reg >> 2) & 3, 2, 2, QChar('0')).arg(reg & 3, 2, 2, QChar('0')), Qt::ElideRight, width() - 16));
    }
    int x = 8;
    for (int index = 0; index < 4; ++index) {
        const QRect swatch(x, 44, 14, 14);
        painter.fillRect(swatch, indexColor(video_, palette_, std::uint8_t(index)));
        painter.setPen(style::border); painter.drawRect(swatch);
        painter.setPen(style::text);
        const bool clear = isObject(palette_) && index == 0;
        const auto text = palette_ == TilePalette::ColorNumbers ? QString("%1").arg(index)
                        : clear ? QString("%1 → clear").arg(index) : QString("%1 → shade %2").arg(index).arg(shade(paletteRegister(video_, palette_), std::uint8_t(index)));
        painter.drawText(x + 18, 56, text);
        x += 18 + metrics.horizontalAdvance(text) + 14;
    }
    // Column headings, then one row per pair of bytes.
    painter.setPen(style::muted);
    const int header = l.rowsTop - 6;
    painter.drawText(8, header, "address");
    painter.drawText(l.lowHexX, header, metrics.elidedText("bit-0 plane", Qt::ElideRight, 8 * l.bit + 28));
    painter.drawText(l.highHexX, header, metrics.elidedText("bit-1 plane", Qt::ElideRight, 8 * l.bit + 28));
    painter.drawText(l.pixelX, header, metrics.elidedText("= colour numbers", Qt::ElideRight, width() - l.pixelX));
    for (int row = 0; row < 8; ++row) {
        const int top = l.rowsTop + row * l.rowHeight;
        const auto address = tileAddress(tile_) + row * 2;
        const auto low = video_.vram[std::size_t(address - 0x8000)], high = video_.vram[std::size_t(address - 0x8000 + 1)];
        painter.setPen(style::muted);
        painter.drawText(QRect(8, top, 50, l.pixel), Qt::AlignVCenter, q(hex(address)));
        painter.setPen(style::text);
        painter.drawText(QRect(l.lowHexX, top, 28, l.pixel), Qt::AlignVCenter, q(hex(low, 2)).mid(1));
        painter.drawText(QRect(l.highHexX, top, 28, l.pixel), Qt::AlignVCenter, q(hex(high, 2)).mid(1));
        const int bitTop = top + (l.pixel - l.bit) / 2;
        for (int c = 0; c < 8; ++c) {
            for (auto [byte, left, colour] : {std::tuple{low, l.lowX, style::write}, std::tuple{high, l.highX, style::execute}}) {
                const bool set = (byte >> (7 - c)) & 1;
                const QRect box(left + c * l.bit, bitTop, l.bit - 2, l.bit - 2);
                painter.fillRect(box, set ? colour : style::panel);
                painter.setPen(style::border); painter.drawRect(box);
                if (l.bit >= 13) { painter.setPen(set ? QColor("#10171d") : style::muted); painter.drawText(box, Qt::AlignCenter, set ? "1" : "0"); }
            }
            const auto index = pixels[row * 8 + c];
            const QRect box(l.pixelX + c * l.pixel, top, l.pixel - 1, l.pixel - 1);
            const auto colour = indexColor(video_, palette_, index);
            painter.fillRect(box, colour);
            painter.setPen(readable(colour));
            painter.drawText(box, Qt::AlignCenter, QString::number(index));
        }
    }
}
void TileDetail::mousePressEvent(QMouseEvent* event) {
    const auto l = layout();
    const int row = (int(event->position().y()) - l.rowsTop) / l.rowHeight;
    if (event->position().y() >= l.rowsTop && row >= 0 && row < 8) emit addressActivated(std::uint16_t(tileAddress(tile_) + row * 2));
}

TileInspector::TileInspector(QWidget* parent) : QWidget(parent) {
    setObjectName("tileInspector");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    caption_ = new QLabel;
    caption_->setObjectName("tileCaption");
    caption_->setWordWrap(true);
    caption_->setText("Decoded from VRAM and OAM storage at the shared cursor. Each tile row is two bytes, a bit-0 plane and a bit-1 plane; "
                      "together they give each pixel a colour number, which a palette register maps to a shade.");
    root->addWidget(caption_);
    auto* split = new QSplitter(Qt::Horizontal);
    root->addWidget(split, 1);
    detail_ = new TileDetail;
    split->addWidget(detail_);
    auto* side = new QWidget;
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    oam_ = new QTableWidget(spriteCount, 5);
    oam_->setObjectName("oamTable");
    oam_->setHorizontalHeaderLabels({"#", "Y+16", "X+8", "Tile", "Attr"});
    oam_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    oam_->setSelectionBehavior(QAbstractItemView::SelectRows);
    oam_->setSelectionMode(QAbstractItemView::SingleSelection);
    oam_->verticalHeader()->hide();
    oam_->verticalHeader()->setDefaultSectionSize(21);
    oam_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    oam_->horizontalHeader()->setMinimumSectionSize(24);
    oam_->setFont(style::monospace());
    oam_->setToolTip("Object Attribute Memory at $FE00: 40 records of Y+16, X+8, tile, attributes. Grey rows are off-screen.");
    for (int r = 0; r < spriteCount; ++r) for (int c = 0; c < 5; ++c) oam_->setItem(r, c, new QTableWidgetItem);
    sideLayout->addWidget(oam_, 2);
    paletteChoice_ = new QComboBox;
    paletteChoice_->setObjectName("tilePalette");
    paletteChoice_->setToolTip("How tile pixels are coloured in the sheet and the detail.");
    paletteChoice_->addItem("Colours: auto (sprite's palette, else BGP)");
    paletteChoice_->addItem("Colours: BGP · background");
    paletteChoice_->addItem("Colours: OBP0 · sprite palette 0");
    paletteChoice_->addItem("Colours: OBP1 · sprite palette 1");
    paletteChoice_->addItem("Colours: colour numbers only");
    sideLayout->addWidget(paletteChoice_);
    auto* blockRow = new QHBoxLayout;
    blockRow->addWidget(new QLabel("Tiles:"));
    blocks_ = new QButtonGroup(this);
    for (int b = 0; b < 3; ++b) {
        auto* button = new QPushButton(q(hex(0x8000 + b * 0x800)));
        button->setObjectName(QString("tileBlock%1").arg(b));
        button->setCheckable(true);
        button->setToolTip(b == 0 ? "Tiles 0–127 at $8000–$87FF: sprite tiles, and background tiles when LCDC bit 4 = 1"
                         : b == 1 ? "Tiles 128–255 at $8800–$8FFF: shared by sprites and background"
                                  : "Tiles 256–383 at $9000–$97FF: background tiles when LCDC bit 4 = 0");
        blocks_->addButton(button, b);
        blockRow->addWidget(button);
    }
    blocks_->button(0)->setChecked(true);
    sideLayout->addLayout(blockRow);
    sheet_ = new TileSheet;
    sideLayout->addWidget(sheet_, 3);
    auto* legend = new QLabel("Pink: selected · teal outline: used by an on-screen sprite · corner mark: in the background map");
    legend->setObjectName("hint"); legend->setWordWrap(true);
    sideLayout->addWidget(legend);
    split->addWidget(side);
    split->setStretchFactor(0, 4); split->setStretchFactor(1, 3);
    split->setSizes({430, 320}); // Room for 10 px bit cells and a 2× tile sheet at the default size.

    connect(oam_, &QTableWidget::cellClicked, this, [this](int row, int) { selectSprite(row); emit spriteSelected(row); });
    connect(sheet_, &TileSheet::tileClicked, this, [this](int tile) {
        sprite_ = -1; oam_->clearSelection(); selectTile(tile); emit spriteSelected(-1);
    });
    connect(paletteChoice_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateViews(); });
    connect(blocks_, &QButtonGroup::idClicked, sheet_, &TileSheet::setBlock);
    connect(sheet_, &TileSheet::blockChanged, this, [this](int block) { blocks_->button(block)->setChecked(true); });
    connect(detail_, &TileDetail::addressActivated, this, &TileInspector::addressActivated);
}
int TileInspector::selectedTile() const { return sheet_->selected(); }
TilePalette TileInspector::palette() const {
    switch (paletteChoice_->currentIndex()) {
    case 1: return TilePalette::Background;
    case 2: return TilePalette::Object0;
    case 3: return TilePalette::Object1;
    case 4: return TilePalette::ColorNumbers;
    default:
        if (sprite_ >= 0) return sprite(video_.oam, sprite_).palette() ? TilePalette::Object1 : TilePalette::Object0;
        return TilePalette::Background;
    }
}
void TileInspector::setSnapshot(const Snapshot& snapshot) {
    video_ = snapshot.video;
    haveData_ = true;
    const int height = video_.lcdc & 0x04 ? 16 : 8;
    for (int i = 0; i < spriteCount; ++i) {
        const auto s = sprite(video_.oam, i);
        const bool visible = s.onScreen(height);
        const QStringList values{QString::number(i), QString::number(s.y), QString::number(s.x), QString::number(s.tile), q(hex(s.flags, 2))};
        const auto where = visible ? QString("Sprite %1 on screen at x=%2, y=%3").arg(i).arg(s.screenX()).arg(s.screenY())
                                   : QString("Sprite %1 is off-screen").arg(i);
        for (int c = 0; c < 5; ++c) {
            auto* item = oam_->item(i, c);
            item->setText(values[c]);
            item->setToolTip(where);
            item->setForeground(visible ? style::text : style::muted);
        }
    }
    updateViews();
}
void TileInspector::selectSprite(int index) {
    sprite_ = index;
    if (index < 0) { oam_->clearSelection(); updateViews(); return; }
    oam_->selectRow(index);
    oam_->scrollToItem(oam_->item(index, 0));
    auto s = sprite(video_.oam, index);
    sheet_->setSelected((video_.lcdc & 0x04) ? s.tile & 0xFE : s.tile);
    updateViews();
}
void TileInspector::selectTile(int tile) {
    sheet_->setSelected(std::clamp(tile, 0, tileCount - 1));
    updateViews();
}
void TileInspector::updateViews() {
    if (!haveData_) return;
    const int height = video_.lcdc & 0x04 ? 16 : 8;
    std::set<int> spriteTiles, mapTiles;
    QStringList users;
    const int tile = sheet_->selected();
    for (int i = 0; i < spriteCount; ++i) {
        const auto s = sprite(video_.oam, i);
        if (!s.onScreen(height)) continue;
        const int first = height == 16 ? s.tile & 0xFE : s.tile;
        for (int t = first; t < first + height / 8; ++t) {
            spriteTiles.insert(t);
            if (t == tile) users << QString::number(i);
        }
    }
    const std::size_t map = (video_.lcdc & 0x08) ? 0x1C00 : 0x1800;
    int mapUses = 0;
    for (std::size_t i = 0; i < 1024; ++i) {
        const int t = backgroundTile(video_.vram[map + i], video_.lcdc & 0x10);
        mapTiles.insert(t);
        mapUses += t == tile;
    }
    QString usage;
    if (!users.isEmpty()) usage = (users.size() == 1 ? "sprite " : "sprites ") + users.join(", ");
    if (mapUses) usage += QString(usage.isEmpty() ? "" : " · ") + QString("background map ×%1").arg(mapUses);
    if (usage.isEmpty()) usage = "not used by visible sprites or the background map";
    const auto p = palette();
    sheet_->setData(video_, p, spriteTiles, mapTiles);
    detail_->setData(video_, tile, p, usage);
}
}
