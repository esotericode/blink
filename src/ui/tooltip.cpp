#include "ui/tooltip.hpp"
#include "teaching/glossary.hpp"
#include "ui/style.hpp"
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCursor>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QHelpEvent>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTabBar>
#include <QTextDocument>
#include <QTimer>
#include <algorithm>

namespace observatory::tips {
namespace {
const QChar separator(0x1F);
const char* customProperty = "observatoryCustomTip";
const QColor cardBack("#121d26"), footerBack("#18262f"), edge("#3a5162");

struct Parts { QString title, body, hint; };
Parts split(const QString& text) {
    const auto pieces = text.split(separator);
    if (pieces.size() >= 2) return {pieces[0], pieces[1], pieces.size() > 2 ? pieces[2] : QString()};
    return {{}, text, {}};
}
QString rich(const QString& text) {
    return Qt::mightBeRichText(text) ? text : text.toHtmlEscaped().replace('\n', "<br>");
}

// The card: title in the accent colour, the explanation, and an optional hint
// in a footer band. Opaque and rounded by a mask; optionally translucent with
// a soft shadow (see the constructor).
class Card : public QWidget {
public:
    Card() : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus |
                              Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint) {
        setObjectName("observatoryTip");
        setAttribute(Qt::WA_ShowWithoutActivating);
        // Opaque with a rounded mask by default: verified on X11 and Windows (Wine),
        // where a translucent card stayed invisible. OBSERVATORY_TOOLTIPS=soft opts
        // into a translucent card with a shadow on composited desktops.
        translucent_ = qEnvironmentVariable("OBSERVATORY_TOOLTIPS") == "soft";
        if (translucent_) setAttribute(Qt::WA_TranslucentBackground);
        fade_ = new QPropertyAnimation(this, "windowOpacity", this);
        fade_->setDuration(120);
        fade_->setStartValue(0.0);
        fade_->setEndValue(1.0);
    }
    const QString& source() const { return source_; }
    void setText(const QString& text) {
        if (text == source_ && !card_.isEmpty()) return;
        source_ = text;
        const auto parts = split(text);
        auto font = QApplication::font();
        main_.setDefaultFont(font);
        auto small = font; small.setPointSizeF(std::max(7.0, font.pointSizeF() - 0.5));
        hint_.setDefaultFont(small);
        main_.setDocumentMargin(0);
        hint_.setDocumentMargin(0);
        QString html;
        if (!parts.title.isEmpty()) {
            html += QString("<div style='color:%1; font-weight:600; font-size:%2pt;'>%3</div>")
                        .arg(style::accent.name()).arg(font.pointSizeF() + 0.5).arg(parts.title.toHtmlEscaped());
            if (!parts.body.isEmpty()) html += "<div style='font-size:3pt;'>&nbsp;</div>";
        }
        if (!parts.body.isEmpty()) html += QString("<div style='color:%1;'>%2</div>").arg(style::text.name(), rich(parts.body));
        main_.setHtml(html);
        hasHint_ = !parts.hint.isEmpty();
        hint_.setHtml(hasHint_ ? QString("<span style='color:%1;'>&#9656;&nbsp;</span><span style='color:%2;'>%3</span>")
                                     .arg(style::accent.name(), style::muted.name(), rich(parts.hint))
                               : QString());
        main_.setTextWidth(-1);
        hint_.setTextWidth(-1);
        const qreal ideal = std::max(main_.idealWidth(), hasHint_ ? hint_.idealWidth() : 0.0);
        textWidth_ = std::clamp(ideal + 1, 120.0, 340.0);
        main_.setTextWidth(textWidth_);
        hint_.setTextWidth(textWidth_);
        mainHeight_ = main_.size().height();
        hintHeight_ = hasHint_ ? hint_.size().height() : 0;
        card_ = QSize(int(stripe + padX + textWidth_ + padX + 0.5),
                      int(padY + mainHeight_ + padY + (hasHint_ ? hintPad + hintHeight_ + hintPad : 0) + 0.5));
        resize(card_.width() + 2 * shadow(), card_.height() + 2 * shadow());
        if (!translucent_) {
            QPainterPath path; path.addRoundedRect(QRectF(rect()), radius(), radius());
            setMask(QRegion(path.toFillPolygon().toPolygon()));
        }
        update();
    }
    void showAt(const QPoint& cursor) {
        const auto* screen = QGuiApplication::screenAt(cursor);
        const QRect bounds = screen ? screen->availableGeometry() : QRect(QPoint(), QSize(4096, 4096));
        QPoint corner = cursor + QPoint(14, 20) - QPoint(shadow(), shadow());
        if (corner.x() + width() > bounds.right()) corner.setX(std::max(bounds.left(), bounds.right() - width()));
        if (corner.y() + height() > bounds.bottom()) corner.setY(cursor.y() - height() - 6 + shadow());
        corner.setY(std::max(bounds.top(), corner.y()));
        move(corner);
        if (!isVisible()) {
            setWindowOpacity(0.0);
            show();
            fade_->start();
        }
        raise();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF card(shadow(), shadow(), card_.width(), card_.height());
        for (int i = shadow(); i > 0; --i) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, int(36.0 * (1.0 - double(i) / shadow()) * (1.0 - double(i) / shadow())) + 2));
            p.drawRoundedRect(card.adjusted(-i, -i + 3, i, i + 3), radius() + i, radius() + i);
        }
        QPainterPath path;
        path.addRoundedRect(card, radius(), radius());
        p.fillPath(path, cardBack);
        p.save();
        p.setClipPath(path);
        const double footerTop = card.top() + padY + mainHeight_ + padY;
        if (hasHint_) {
            p.fillRect(QRectF(card.left(), footerTop, card.width(), card.bottom() - footerTop), footerBack);
            p.setPen(QPen(edge, 1));
            p.drawLine(QPointF(card.left() + stripe, footerTop + 0.5), QPointF(card.right(), footerTop + 0.5));
        }
        p.fillRect(QRectF(card.left(), card.top(), stripe, card.height()), style::accent);
        p.restore();
        p.setPen(QPen(edge, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), radius(), radius());
        p.translate(card.left() + stripe + padX, card.top() + padY);
        main_.drawContents(&p);
        if (hasHint_) {
            p.translate(0, mainHeight_ + padY + hintPad);
            hint_.drawContents(&p);
        }
    }
private:
    static constexpr int stripe = 3, padX = 12, padY = 9, hintPad = 7;
    int shadow() const { return translucent_ ? 10 : 0; }
    double radius() const { return translucent_ ? 8 : 6; }
    QTextDocument main_, hint_;
    QString source_;
    QSize card_;
    qreal textWidth_{}, mainHeight_{}, hintHeight_{};
    bool translucent_{}, hasHint_{};
    QPropertyAnimation* fade_{};
};

// Receives every tooltip request in the application and answers it with the card.
class Router : public QObject {
public:
    Router() : QObject(qApp) {
        poll_.setInterval(80);
        connect(&poll_, &QTimer::timeout, this, [this] {
            if (!card_ || !card_->isVisible()) { poll_.stop(); return; }
            if (!owner_ || !owner_->isVisible() || !area_.contains(QCursor::pos())) { hide(); retarget(QCursor::pos()); }
        });
        connect(qApp, &QApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) hide();
        });
    }
    void show(QWidget* owner, const QPoint& globalPos, const QString& text, const QRect& area) {
        if (text.isEmpty() || !owner) { hide(); return; }
        if (!card_) card_ = new Card;
        card_->setText(text);
        owner_ = owner;
        area_ = QRect(owner->mapToGlobal(area.topLeft()), area.size());
        card_->showAt(globalPos);
        poll_.start();
    }
    void hide() {
        if (card_ && card_->isVisible()) card_->hide();
        poll_.stop();
    }
    QWidget* card() const { return card_ && card_->isVisible() ? card_ : nullptr; }
    QString text() const { return card() ? card_->source() : QString(); }
protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        switch (event->type()) {
        case QEvent::ToolTip:
            if (auto* widget = qobject_cast<QWidget*>(object)) return request(widget, static_cast<QHelpEvent*>(event));
            break;
        case QEvent::MouseButtonPress: case QEvent::MouseButtonDblClick: case QEvent::Wheel:
        case QEvent::KeyPress: case QEvent::WindowDeactivate:
            hide();
            break;
        case QEvent::Leave:
            if (object == owner_ && card() && !area_.contains(QCursor::pos())) hide();
            break;
        default: break;
        }
        return false;
    }
private:
    // Warm mode: after a tip, moving to something else explains it at once.
    void retarget(const QPoint& globalPos) {
        if (retargeting_) return;
        auto* widget = QApplication::widgetAt(globalPos);
        if (!widget || widget->window() == card_) return;
        retargeting_ = true;
        QHelpEvent help(QEvent::ToolTip, widget->mapFromGlobal(globalPos), globalPos);
        QApplication::sendEvent(widget, &help);
        retargeting_ = false;
    }
    // The nearest widget (this one or an ancestor) with something to say.
    bool fallback(QWidget* widget, QHelpEvent* help) {
        for (auto* w = widget; w; w = w->parentWidget()) {
            if (w->property(customProperty).toBool()) {
                QHelpEvent forwarded(QEvent::ToolTip, w->mapFromGlobal(help->globalPos()), help->globalPos());
                QApplication::sendEvent(w, &forwarded);
                return true;
            }
            if (!w->toolTip().isEmpty()) { show(w, help->globalPos(), w->toolTip(), w->rect()); return true; }
            if (w->isWindow()) break;
        }
        hide();
        return true;
    }
    bool request(QWidget* w, QHelpEvent* help) {
        if (card_ && w->window() == card_) return true;
        if (w->property(customProperty).toBool()) return false; // it calls tips::show itself
        const auto pos = help->pos();
        auto* parent = w->parentWidget();
        if (auto* header = qobject_cast<QHeaderView*>(parent); header && w == header->viewport()) {
            const int section = header->logicalIndexAt(pos);
            const auto text = section >= 0 && header->model()
                ? header->model()->headerData(section, header->orientation(), Qt::ToolTipRole).toString() : QString();
            if (text.isEmpty()) return fallback(header->parentWidget() ? header->parentWidget() : header, help);
            const int start = header->sectionViewportPosition(section), size = header->sectionSize(section);
            show(w, help->globalPos(), text, header->orientation() == Qt::Horizontal ? QRect(start, 0, size, w->height())
                                                                                     : QRect(0, start, w->width(), size));
            return true;
        }
        if (auto* view = qobject_cast<QAbstractItemView*>(parent); view && w == view->viewport()) {
            const auto index = view->indexAt(pos);
            const auto text = index.isValid() ? index.data(Qt::ToolTipRole).toString() : QString();
            if (text.isEmpty()) return fallback(view, help);
            show(w, help->globalPos(), text, view->visualRect(index));
            return true;
        }
        if (auto* bar = qobject_cast<QTabBar*>(w)) {
            const int tab = bar->tabAt(pos);
            QString text;
            if (tab >= 0) {
                // Tabs of docked panels (Qt sets their tooltip to the bare title): explain the panel.
                for (auto* window = bar->window(); auto* dock : window->findChildren<QDockWidget*>()) {
                    if (dock->windowTitle() == bar->tabText(tab)) text = dock->property("panelTip").toString();
                }
                if (text.isEmpty()) text = bar->tabToolTip(tab);
            }
            if (text.isEmpty()) { hide(); return true; }
            show(w, help->globalPos(), text, bar->tabRect(tab));
            return true;
        }
        if (auto* menu = qobject_cast<QMenu*>(w)) {
            auto* action = menu->actionAt(pos);
            QString text;
            if (action && !action->menu()) {
                auto label = action->text(); label.remove('&');
                if (action->toolTip() != label && action->toolTip() != action->text()) text = action->toolTip();
            }
            if (text.isEmpty()) { hide(); return true; }
            show(w, help->globalPos(), text, menu->actionGeometry(action));
            return true;
        }
        if (auto* dock = qobject_cast<QDockWidget*>(w)) {
            if (dock->widget() && pos.y() < dock->widget()->geometry().top()) {
                const auto text = dock->property("panelTip").toString();
                if (text.isEmpty()) { hide(); return true; }
                show(w, help->globalPos(), text, QRect(0, 0, dock->width(), dock->widget()->geometry().top()));
                return true;
            }
            return false;
        }
        if (!w->toolTip().isEmpty()) { show(w, help->globalPos(), w->toolTip(), w->rect()); return true; }
        return false; // Qt offers it to the parent next.
    }
    QPointer<Card> card_;
    QPointer<QWidget> owner_;
    QRect area_;
    QTimer poll_;
    bool retargeting_{};
};
Router* router = nullptr;
}

QString make(const QString& title, const QString& body, const QString& hint) {
    return title + separator + body + (hint.isEmpty() ? QString() : separator + hint);
}
QString make(const Explanation& e) {
    if (e.empty()) return {};
    return make(QString::fromStdString(e.title), QString::fromStdString(e.body), QString::fromStdString(e.hint));
}
QString key(const std::string& k) { return make(glossary(k)); }
void install() {
    if (router) return;
    router = new Router;
    qApp->installEventFilter(router);
}
void show(QWidget* owner, const QPoint& globalPos, const QString& text, const QRect& area) {
    if (router) router->show(owner, globalPos, text, area);
}
void hide() { if (router) router->hide(); }
void setCustom(QWidget* widget) { widget->setProperty(customProperty, true); }
QWidget* current() { return router ? router->card() : nullptr; }
QString currentText() { return router ? router->text() : QString(); }
} // namespace observatory::tips
