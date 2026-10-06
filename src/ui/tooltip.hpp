#pragma once
#include <QPointer>
#include <QRect>
#include <QString>
#include <string>

class QWidget;

// Explanatory tooltips: a card with a title, a plain-language explanation, and
// an optional hint, shown for every tooltip in the application. Widgets keep
// using setToolTip()/ToolTipRole; custom-painted widgets call tips::show().
namespace observatory {
struct Explanation;
namespace tips {
// Encodes a structured tip as tooltip text (title, body, hint separated by U+001F).
// Text without separators is shown as a body-only tip.
QString make(const QString& title, const QString& body, const QString& hint = {});
QString make(const Explanation& explanation);
// The glossary entry for `key` as tooltip text (see teaching/glossary.hpp).
QString key(const std::string& key);
// Route every tooltip in the application through the card. Call once.
void install();
// For custom-painted widgets: show `text` for the region `area` (owner
// coordinates); the tip stays while the pointer is inside it.
void show(QWidget* owner, const QPoint& globalPos, const QString& text, const QRect& area);
void hide();
// Marks a widget that answers QEvent::ToolTip itself by calling show().
void setCustom(QWidget* widget);
// For tests and screenshots: the visible card (or null) and its source text.
QWidget* current();
QString currentText();
} // namespace tips
} // namespace observatory
