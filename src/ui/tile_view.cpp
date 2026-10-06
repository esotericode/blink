#include "ui/tile_view.hpp"
#include "ui/tile_map_view.hpp"
#include <QTabWidget>
#include "ui/style.hpp"
#include "ui/tooltip.hpp"
#include "teaching/glossary.hpp"
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
        if (t < 0) { tips::show(this, help->globalPos(), tips::key("tiles.sheet"), rect()); return true; }
        QString body = QString("An 8×8 picture stored as 16 bytes at %1. ").arg(tileRange(t));
        if (t < 128) body += "This block is used by sprites, and by the background when LCDC bit 4 = 1.";
        else if (t < 256) body += "This block is shared by sprites and the background.";
        else body += "This block is used by the background when LCDC bit 4 = 0.";
        QStringList uses;
        if (spriteTiles_.count(t)) uses << "an on-screen sprite";
        if (mapTiles_.count(t)) uses << "the background map";
        if (!uses.isEmpty()) body += " Used by " + uses.join(" and ") + ".";
        const int s = scale(), cell = 8 * s + 1, slot = t - block_ * 128;
        tips::show(this, help->globalPos(), tips::make(QString("Tile %1").arg(t), body, "Click to see its bits."),
                   QRect(1 + (slot % 16) * cell, 1 + (slot / 16) * cell, 8 * s, 8 * s));
        return true;
    }
    return QWidget::event(event);
}

TileDetail::TileDetail(QWidget* parent) : QWidget(parent) {
    setObjectName("tileDetail");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    tips::setCustom(this);
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
bool TileDetail::event(QEvent* event) {
    if (event->type() != QEvent::ToolTip) return QWidget::event(event);
    auto* help = static_cast<QHelpEvent*>(event);
    const auto l = layout();
    const QPoint p = help->pos();
    const int row = (p.y() - l.rowsTop) / std::max(1, l.rowHeight);
    if (p.y() < l.rowsTop || row >= 8) {
        tips::show(this, help->globalPos(), p.y() >= 22 && p.y() < 62 ? tips::key("tiles.palette")
            : tips::make(QString("Tile %1").arg(tile_), "Each of the 8 rows is two bytes. Bit n of the first byte and bit n of "
                         "the second byte together give pixel n its colour number.", "Hover a bit or a pixel below for details."),
            p.y() < l.rowsTop ? QRect(0, 0, width(), l.rowsTop) : QRect(0, l.rowsTop + 8 * l.rowHeight, width(), height()));
        return true;
    }
    const auto address = std::uint16_t(tileAddress(tile_) + row * 2);
    const auto low = video_.vram[std::size_t(address - 0x8000)], high = video_.vram[std::size_t(address - 0x8000 + 1)];
    const int top = l.rowsTop + row * l.rowHeight;
    auto bit = [](std::uint8_t byte, int c) { return (byte >> (7 - c)) & 1; };
    auto binary = [](std::uint8_t byte) { return QString("%1").arg(byte, 8, 2, QChar('0')); };
    if (p.x() < l.lowHexX) {
        tips::show(this, help->globalPos(), tips::make(QString("Row %1 · %2").arg(row).arg(q(hex(address))),
            QString("This row is two bytes: %1 at %2 (bit plane 0) and %3 at %4 (bit plane 1).")
                .arg(q(hex(low, 2)), q(hex(address)), q(hex(high, 2)), q(hex(std::uint16_t(address + 1)))),
            "Click to view them in the Memory panel."), QRect(0, top, l.lowHexX, l.rowHeight));
        return true;
    }
    for (auto [byte, hexX, bitsX, plane, key] : {std::tuple{low, l.lowHexX, l.lowX, 0, "tiles.low"}, std::tuple{high, l.highHexX, l.highX, 1, "tiles.high"}}) {
        if (p.x() < hexX || p.x() >= bitsX + 8 * l.bit) continue;
        const int c = p.x() < bitsX ? -1 : (p.x() - bitsX) / l.bit;
        const Explanation e = glossary(key);
        QString body = q(e.body) + QString(" Row %1's byte is %2 = %3 in binary.").arg(row).arg(q(hex(byte, 2)), binary(byte));
        if (c >= 0) body += QString(" Bit for pixel %1 (counting from the left): %2.").arg(c).arg(bit(byte, c));
        tips::show(this, help->globalPos(), tips::make(q(e.title), body),
                   c >= 0 ? QRect(bitsX + c * l.bit, top, l.bit, l.rowHeight) : QRect(hexX, top, bitsX - hexX, l.rowHeight));
        (void)plane;
        return true;
    }
    if (p.x() >= l.pixelX && p.x() < l.pixelX + 8 * l.pixel) {
        const int c = (p.x() - l.pixelX) / l.pixel;
        const int h = bit(high, c), lo = bit(low, c), n = h * 2 + lo;
        QString result;
        if (palette_ == TilePalette::ColorNumbers) result = "No palette is applied in this view.";
        else if (isObject(palette_) && n == 0) result = QString("For sprites, colour 0 is always transparent.");
        else result = QString("%1 turns colour %2 into shade %3 (0 = lightest, 3 = darkest).")
                          .arg(paletteName(palette_)).arg(n).arg(shade(paletteRegister(video_, palette_), std::uint8_t(n)));
        tips::show(this, help->globalPos(), tips::make(QString("Pixel %1 of row %2 · colour %3").arg(c).arg(row).arg(n),
            QString("High bit %1 × 2 + low bit %2 = colour number %3. ").arg(h).arg(lo).arg(n) + result),
            QRect(l.pixelX + c * l.pixel, top, l.pixel, l.pixel));
        return true;
    }
    tips::show(this, help->globalPos(), tips::key("tiles.pixels"), QRect(0, top, width(), l.rowHeight));
    return true;
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
    caption_->setProperty("informationTopic", "vram");
    caption_->setWordWrap(true);
    caption_->setText("Decoded from VRAM and OAM storage at the shared cursor. Each tile row is two bytes, a bit-0 plane and a bit-1 plane; "
                      "together they give each pixel a colour number, which a palette register maps to a shade.");
    root->addWidget(caption_);
    auto* split = new QSplitter(Qt::Horizontal);
    auto* tabs = new QTabWidget; tabs->setObjectName("graphicsTabs");
    tabs->addTab(split, "Patterns and sprites");
    root->addWidget(tabs, 1);
    detail_ = new TileDetail;
    split->addWidget(detail_);
    auto* side = new QWidget;
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    // Where OAM's contents came from: CPU stores or a DMA copy (see Snapshot::dma).
    oamSource_ = new QLabel;
    oamSource_->setObjectName("oamSource");
    oamSource_->setWordWrap(true);
    oamSource_->setTextFormat(Qt::RichText);
    oamSource_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    oamSource_->setToolTip(tips::key("tiles.source"));
    connect(oamSource_, &QLabel::linkActivated, this, [this](const QString& link) {
        bool ok = false;
        const auto a = link.startsWith("addr:") ? link.mid(5).toUInt(&ok, 16) : 0u;
        if (ok) emit addressActivated(std::uint16_t(a));
    });
    sideLayout->addWidget(oamSource_);
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
    oam_->setToolTip(tips::key("tiles.oam"));
    {
        const char* keys[] = {"tiles.col.index", "tiles.col.y", "tiles.col.x", "tiles.col.tile", "tiles.col.attr"};
        for (int c = 0; c < 5; ++c) oam_->horizontalHeaderItem(c)->setToolTip(tips::key(keys[c]));
    }
    for (int r = 0; r < spriteCount; ++r) for (int c = 0; c < 5; ++c) oam_->setItem(r, c, new QTableWidgetItem);
    sideLayout->addWidget(oam_, 2);
    paletteChoice_ = new QComboBox;
    paletteChoice_->setObjectName("tilePalette");
    paletteChoice_->setProperty("informationTopic", "palettes");
    paletteChoice_->setToolTip(tips::key("tiles.palette"));
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
        button->setProperty("informationTopic", "tile-data");
        button->setCheckable(true);
        button->setToolTip(tips::make(b == 0 ? "Tiles 0–127 · $8000–$87FF" : b == 1 ? "Tiles 128–255 · $8800–$8FFF" : "Tiles 256–383 · $9000–$97FF",
            b == 0 ? "Used by sprites, and by the background when LCDC bit 4 = 1."
          : b == 1 ? "Shared by sprites and the background."
                   : "Used by the background when LCDC bit 4 = 0.", q(glossary("tiles.block").body)));
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

    auto* mapPage = new QWidget;
    auto* mapLayout = new QVBoxLayout(mapPage); mapLayout->setContentsMargins(0, 4, 0, 0);
    auto* mapChoice = new QComboBox; mapChoice->setObjectName("tileMapChoice");
    mapChoice->addItems({"Background map (LCDC selection)", "Window map (LCDC selection)", "Map at $9800", "Map at $9C00"});
    mapChoice->setToolTip(tips::key("tiles.maps"));
    mapLayout->addWidget(mapChoice);
    maps_ = new TileMapView; mapLayout->addWidget(maps_, 1);
    mapCaption_ = new QLabel; mapCaption_->setObjectName("tileMapCaption"); mapCaption_->setWordWrap(true);
    mapLayout->addWidget(mapCaption_);
    tabs->addTab(mapPage, "Background / window maps");
    connect(mapChoice, qOverload<int>(&QComboBox::activated), maps_, &TileMapView::setMode);
    connect(maps_, &TileMapView::cellSelected, this, [this](std::uint16_t address, int tile) {
        selectTile(tile); mapAddress_ = address; updateViews();
        emit spriteSelected(-1); emit mapCellSelected(address, tile);
    });
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) emit mapCellSelected(maps_->selectedAddress(), maps_->selectedTile());
    });

    connect(oam_, &QTableWidget::cellClicked, this, [this](int row, int) { selectSprite(row); emit spriteSelected(row); });
    connect(sheet_, &TileSheet::tileClicked, this, [this](int tile) {
        selectTile(tile); emit spriteSelected(-1); emit tileSelected(tile);
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
    auto link = [](std::uint16_t a) {
        return QString("<a href='addr:%1' style='color:%2'>%3</a>").arg(a, 4, 16, QChar('0')).arg(style::accent.name(), q(hex(a)));
    };
    if (snapshot.dmaCopying) {
        oamSource_->setText(QString("<b>OAM DMA copying now</b> from %1 (requested at t=%2).")
            .arg(link(snapshot.dmaCopying->sourceOf(0))).arg(snapshot.dmaCopying->requestStartTicks));
    } else if (!snapshot.dma.empty()) {
        const auto& d = snapshot.dma.back();
        oamSource_->setText(QString("Most recent observed <b>DMA</b> request: %1–%2 (often a shadow sprite table), "
                                    "requested by %3 at t=%4; %5 OAM bytes differed when checked. Later CPU stores may change OAM.")
            .arg(link(d.sourceOf(0)), q(hex(d.sourceOf(oamBytes - 1))),
                 d.instruction ? q(hex(d.instruction->pc)).toHtmlEscaped() : QString("an unrecorded instruction"))
            .arg(d.requestStartTicks).arg(d.changed()));
    } else {
        oamSource_->setText(snapshot.traceEnabled ? "No OAM DMA request captured in this window. Earlier or unobserved copies may still have changed OAM."
                                                  : "Capture writes is off: OAM DMA copies are not recorded.");
    }
    haveData_ = true;
    const int height = video_.lcdc & 0x04 ? 16 : 8;
    for (int i = 0; i < spriteCount; ++i) {
        const auto s = sprite(video_.oam, i);
        const bool visible = s.onScreen(height);
        const QStringList values{QString::number(i), QString::number(s.y), QString::number(s.x), QString::number(s.tile), q(hex(s.flags, 2))};
        const auto where = visible ? QString("Sprite %1's rectangle overlaps the screen at x=%2, y=%3").arg(i).arg(s.screenX()).arg(s.screenY())
                                   : QString("Sprite %1 is off-screen").arg(i);
        for (int c = 0; c < 5; ++c) {
            auto* item = oam_->item(i, c);
            item->setText(values[c]);
            item->setToolTip(tips::make(QString("Sprite %1 · OAM %2").arg(i).arg(q(hex(std::uint16_t(0xFE00 + i * 4)))),
                where + QString(": Y+16 = %1, X+8 = %2, tile %3, attributes %4.").arg(s.y).arg(s.x).arg(s.tile).arg(q(hex(s.flags, 2))),
                "Click to show its tile below."));
            item->setForeground(visible ? style::text : style::muted);
        }
    }
    updateViews();
}
void TileInspector::selectSprite(int index) {
    mapAddress_ = -1;
    sprite_ = index >= 0 && index < spriteCount ? index : -1;
    index = sprite_;
    if (index < 0) { oam_->clearSelection(); updateViews(); return; }
    oam_->selectRow(index);
    oam_->scrollToItem(oam_->item(index, 0));
    auto s = sprite(video_.oam, index);
    sheet_->setSelected((video_.lcdc & 0x04) ? s.tile & 0xFE : s.tile);
    updateViews();
}
void TileInspector::selectTile(int tile) {
    mapAddress_ = -1;
    sprite_ = -1; oam_->clearSelection();
    sheet_->setSelected(std::clamp(tile, 0, tileCount - 1));
    updateViews();
}
void TileInspector::resetSelection() {
    sprite_ = -1; mapAddress_ = -1; haveData_ = false; oam_->clearSelection();
    sheet_->setSelected(2); paletteChoice_->setCurrentIndex(0);
    findChild<QTabWidget*>("graphicsTabs")->setCurrentIndex(0);
}
void TileInspector::updateViews() {
    if (!haveData_) return;
    maps_->setSnapshot(video_);
    mapCaption_->setText(QString("Map %1 · 32×32 cells · BGP shades · reconstructed at State now. Click a cell for its map entry and pattern. No viewport, sprites, or historical pixel attribution.").arg(q(hex(maps_->base()))));
    if (mapAddress_ >= 0x9800 && mapAddress_ <= 0x9FFF)
        sheet_->setSelected(backgroundTile(video_.vram[std::size_t(mapAddress_ - 0x8000)], video_.lcdc & 0x10));
    const int height = video_.lcdc & 0x04 ? 16 : 8;
    if (sprite_ >= 0) {
        const auto selected = sprite(video_.oam, sprite_);
        sheet_->setSelected(height == 16 ? selected.tile & 0xFE : selected.tile);
    }
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
