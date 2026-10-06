#include "ui/activity_map.hpp"
#include "teaching/annotations.hpp"
#include "ui/style.hpp"
#include "ui/tooltip.hpp"
#include "teaching/glossary.hpp"
#include <QComboBox>
#include <QHelpEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace observatory {
namespace {
using style::q;
struct Band { int page; const char* label; };
const Band bands[] = {{0x00, "$0000 ROM0"}, {0x40, "$4000 ROMX"}, {0x80, "$8000 VRAM"}, {0xA0, "$A000 cart RAM"},
                      {0xC0, "$C000 WRAM"}, {0xE0, "$E000 echo"}, {0xFE, "$FE00 OAM/IO"}};
QColor tint(std::uint16_t a) {
    if (a < 0x8000) return QColor("#1c2a33");
    if (a < 0xA000) return QColor("#232a3d");
    if (a < 0xC000) return QColor("#14191e");
    if (a < 0xE000) return QColor("#1d3029");
    if (a < 0xFE00) return QColor("#172520");
    return QColor("#2c2535");
}
QColor mix(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}
// Log scale: one write per frame and an 8 KiB clearing loop both stay visible.
double heat(std::uint32_t n, double logMax) {
    return n && logMax > 0 ? 0.35 + 0.65 * std::log1p(double(n)) / logMax : 0.0;
}
}

ActivityMapView::ActivityMapView(QWidget* parent) : QWidget(parent) {
    setObjectName("activityMap");
    setMouseTracking(true);
    tips::setCustom(this);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    map_.writes.assign(0x10000, 0); map_.executions.assign(0x10000, 0);
    rebuild();
}
void ActivityMapView::setMap(const ActivityMap& map, Program program) {
    map_ = map; program_ = program;
    rebuild();
}
void ActivityMapView::setMode(Mode mode) { mode_ = mode; rebuild(); }
void ActivityMapView::setSelectedPage(int page) { page_ = std::clamp(page, 0, 255); update(); }
void ActivityMapView::setSelectedAddress(std::uint16_t address) { selected_ = address; page_ = address >> 8; update(); }
QColor ActivityMapView::cellColor(std::uint16_t a) const {
    auto colour = tint(a);
    if (mode_ != Mode::Writes && map_.executions[a]) colour = mix(colour, style::execute, heat(map_.executions[a], logMaxExecutions_));
    if (mode_ != Mode::Executions && map_.writes[a]) colour = mix(colour, style::write, heat(map_.writes[a], logMaxWrites_));
    return colour;
}
void ActivityMapView::rebuild() {
    const auto logMax = [](const std::vector<std::uint32_t>& counts) {
        return counts.empty() ? 0.0 : std::log1p(double(*std::max_element(counts.begin(), counts.end())));
    };
    logMaxWrites_ = logMax(map_.writes);
    logMaxExecutions_ = logMax(map_.executions);
    overview_ = QImage(256, 256, QImage::Format_RGB32);
    hot_.clear();
    for (int a = 0; a < 0x10000; ++a) {
        const auto colour = cellColor(std::uint16_t(a)).rgb();
        overview_.setPixel(a & 0xFF, a >> 8, colour);
        const bool active = (mode_ != Mode::Writes && map_.executions[a]) || (mode_ != Mode::Executions && map_.writes[a]);
        if (active) hot_.emplace_back(std::uint16_t(a), colour);
    }
    update();
}
ActivityMapView::Geometry ActivityMapView::geometry() const {
    const int gutter = 104;
    const int side = std::max(128, std::min(height() - 12, int((width() - gutter - 24) * 0.52)));
    Geometry g;
    g.overview = QRect(gutter, 6, side, side);
    const int x = g.overview.right() + 50;
    g.cell = std::clamp(std::min((width() - x - 8) / 16, (height() - 90) / 16), 8, 28);
    g.detail = QRect(x, 26, 16 * g.cell, 16 * g.cell);
    return g;
}
std::optional<std::uint16_t> ActivityMapView::overviewAddress(QPoint p) const {
    const auto g = geometry();
    if (!g.overview.contains(p)) return {};
    const int column = (p.x() - g.overview.x()) * 256 / g.overview.width();
    const int row = (p.y() - g.overview.y()) * 256 / g.overview.height();
    return std::uint16_t(row << 8 | column);
}
std::optional<std::uint16_t> ActivityMapView::detailAddress(QPoint p) const {
    const auto g = geometry();
    if (!g.detail.contains(p)) return {};
    const int column = (p.x() - g.detail.x()) / g.cell, row = (p.y() - g.detail.y()) / g.cell;
    return std::uint16_t(page_ << 8 | row << 4 | column);
}
void ActivityMapView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), style::window);
    const auto g = geometry();
    auto font = style::monospace();
    font.setPointSizeF(std::max(7.0, font.pointSizeF() - 1.5));
    painter.setFont(font);
    painter.drawImage(g.overview, overview_);
    // At about one screen pixel per address, a single busy byte would vanish:
    // draw every active address as a marker at least 3 px wide.
    const double unit = g.overview.width() / 256.0;
    if (unit < 3) {
        painter.save();
        painter.setClipRect(g.overview);
        for (const auto& [a, colour] : hot_) {
            const QPointF centre(g.overview.x() + ((a & 0xFF) + 0.5) * unit, g.overview.y() + ((a >> 8) + 0.5) * unit);
            painter.fillRect(QRectF(centre.x() - 1.5, centre.y() - 1.5, 3, 3), QColor::fromRgb(colour));
        }
        painter.restore();
    }
    painter.setPen(style::border);
    painter.drawRect(g.overview.adjusted(-1, -1, 0, 0));
    for (const auto& band : bands) {
        const int y = g.overview.y() + band.page * g.overview.height() / 256;
        painter.setPen(style::border);
        if (band.page) painter.drawLine(g.overview.left() - 6, y, g.overview.right(), y);
        painter.setPen(style::muted);
        painter.drawText(QRect(0, y - 1, g.overview.left() - 8, 14), Qt::AlignRight | Qt::AlignTop, band.label);
    }
    // Selected page: outlined row in the overview, linked to the magnified page.
    const int rowTop = g.overview.y() + page_ * g.overview.height() / 256;
    const int rowHeight = std::max(2, g.overview.height() / 256);
    painter.setPen(QPen(style::highlight, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRect(g.overview.left() - 2, rowTop - 1, g.overview.width() + 3, rowHeight + 1));
    painter.drawLine(g.overview.right() + 2, rowTop, g.detail.left() - 34, g.detail.top() + g.detail.height() / 2);

    painter.setPen(style::accent);
    painter.drawText(QRect(g.detail.left() - 40, 4, g.detail.width() + 40, 18), Qt::AlignLeft,
                     QString("Page %1–%2").arg(q(hex(page_ << 8)), q(hex(page_ << 8 | 0xFF))));
    for (int c = 0; c < 16; ++c) {
        painter.setPen(style::muted);
        painter.drawText(QRect(g.detail.x() + c * g.cell, g.detail.y() - 14, g.cell, 14), Qt::AlignCenter, QString::number(c, 16).toUpper());
    }
    for (int r = 0; r < 16; ++r) {
        painter.setPen(style::muted);
        painter.drawText(QRect(g.detail.x() - 44, g.detail.y() + r * g.cell, 40, g.cell), Qt::AlignRight | Qt::AlignVCenter,
                         q(hex(page_ << 8 | r << 4)).mid(1, 3) + "x");
        for (int c = 0; c < 16; ++c) {
            const auto a = std::uint16_t(page_ << 8 | r << 4 | c);
            const QRect cell(g.detail.x() + c * g.cell, g.detail.y() + r * g.cell, g.cell - 1, g.cell - 1);
            const auto colour = cellColor(a);
            painter.fillRect(cell, colour);
            const auto count = mode_ == Mode::Executions ? map_.executions[a] : map_.writes[a] ? map_.writes[a] : mode_ == Mode::Both ? map_.executions[a] : 0;
            if (count && g.cell >= 18) {
                painter.setPen(colour.lightness() > 120 ? QColor("#10171d") : style::text);
                painter.drawText(cell, Qt::AlignCenter, count > 999 ? QString("1k+") : QString::number(count));
            }
        }
    }
    auto outline = [&](std::uint16_t a, const QColor& colour, int width) {
        if ((a >> 8) != page_) return;
        painter.setPen(QPen(colour, width)); painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRect(g.detail.x() + (a & 15) * g.cell - 1, g.detail.y() + ((a >> 4) & 15) * g.cell - 1, g.cell + 1, g.cell + 1));
    };
    outline(selected_, style::highlight, 2);
    if (hover_) outline(*hover_, style::accent, 1);
    const auto shown = hover_.value_or(selected_);
    auto name = addressName(shown, program_);
    QString info = q(hex(shown)) + (name.empty() ? QString() : "  " + q(name)) + "\n" + q(regionName(shown)) +
        QString("\n%1 write attempts · %2 opcode starts").arg(map_.writes[shown]).arg(map_.executions[shown]);
    painter.setPen(style::text);
    painter.drawText(QRect(g.detail.x() - 44, g.detail.bottom() + 8, width() - g.detail.x() + 40, 60), Qt::AlignLeft | Qt::TextWordWrap, info);
}
void ActivityMapView::mousePressEvent(QMouseEvent* event) {
    const auto p = event->position().toPoint();
    if (auto a = detailAddress(p)) { selected_ = *a; update(); emit addressActivated(*a); return; }
    if (auto a = overviewAddress(p)) { selected_ = *a; page_ = *a >> 8; update(); emit addressActivated(*a); }
}
void ActivityMapView::mouseMoveEvent(QMouseEvent* event) {
    const auto p = event->position().toPoint();
    auto a = detailAddress(p);
    if (!a) a = overviewAddress(p);
    if (a != hover_) { hover_ = a; update(); }
}
void ActivityMapView::leaveEvent(QEvent*) { hover_.reset(); update(); }
bool ActivityMapView::event(QEvent* event) {
    if (event->type() != QEvent::ToolTip) return QWidget::event(event);
    auto* help = static_cast<QHelpEvent*>(event);
    const auto p = help->pos();
    const auto g = geometry();
    auto detail = detailAddress(p);
    const auto a = detail ? detail : overviewAddress(p);
    if (!a) { tips::show(this, help->globalPos(), tips::key("map.view"), rect()); return true; }
    const auto region = regionExplanation(*a);
    const auto io = ioRegisterExplanation(*a);
    const auto name = addressName(*a, program_);
    QString title = q(hex(*a)) + (name.empty() ? QString() : " · " + q(name));
    QString body = QString("%1. %2").arg(q(io.empty() ? region.title : io.title), q(io.empty() ? region.body : io.body));
    QString counts = QString("%1 write attempts · %2 instructions started here").arg(map_.writes[*a]).arg(map_.executions[*a]);
    if (!map_.writesObserved) counts += " (writes not counted: capture is off)";
    // The overview is one pixel per address: keep the tip only for the spot under the pointer.
    const QRect area = detail ? QRect(g.detail.x() + (*a & 15) * g.cell, g.detail.y() + ((*a >> 4) & 15) * g.cell, g.cell, g.cell)
                              : QRect(p - QPoint(2, 2), QSize(5, 5));
    tips::show(this, help->globalPos(), tips::make(title, body, counts + ". Click to inspect."), area);
    return true;
}

ActivityPanel::ActivityPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto* top = new QHBoxLayout;
    interval_ = new QLabel;
    interval_->setObjectName("activityInterval");
    interval_->setProperty("informationTopic", "activity");
    interval_->setWordWrap(true);
    top->addWidget(interval_, 1);
    mode_ = new QComboBox;
    mode_->setObjectName("activityMode");
    mode_->setToolTip(tips::key("map.mode"));
    mode_->addItems({"Writes + opcodes", "Write attempts", "Opcode starts"});
    top->addWidget(mode_);
    auto* clear = new QPushButton("Clear");
    clear->setObjectName("activityClear");
    clear->setToolTip(tips::key("map.clear"));
    top->addWidget(clear);
    root->addLayout(top);
    view_ = new ActivityMapView;
    root->addWidget(view_, 1);
    connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        view_->setMode(index == 1 ? ActivityMapView::Mode::Writes : index == 2 ? ActivityMapView::Mode::Executions : ActivityMapView::Mode::Both);
    });
    connect(clear, &QPushButton::clicked, this, &ActivityPanel::clearRequested);
}
void ActivityPanel::setMap(const ActivityMap& map, Program program) {
    const auto frames = double(map.endTicks - map.startTicks) / 140448.0;
    interval_->setText(QString("Per-address CPU activity over t = %1 … %2 ticks (≈ %3 frame periods). "
                               "<span style='color:%4'>Amber: write attempts</span>%5; <span style='color:%6'>blue: opcode starts</span>. "
                               "Reads, DMA and PPU fetches are not counted. Rows are 256-byte pages; $4000–$7FFF combines every ROM bank "
                               "(the Cartridge panel splits activity by bank).")
        .arg(map.startTicks).arg(map.endTicks).arg(frames, 0, 'f', 1).arg(style::write.name())
        .arg(map.writesObserved ? "" : " (capture off: none counted)").arg(style::execute.name()));
    view_->setMap(map, program);
}
}
