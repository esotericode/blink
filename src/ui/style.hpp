#pragma once
#include <QColor>
#include <QFont>
#include <QString>
#include <string>

// Shared colours and helpers for native widgets and custom-painted views.
// Colour always accompanies a text label or symbol; it never carries meaning alone.
namespace observatory::style {
inline const QColor window{"#111b24"};
inline const QColor panel{"#17232d"};
inline const QColor raised{"#243441"};
inline const QColor border{"#34424e"};
inline const QColor text{"#dce4ea"};
inline const QColor muted{"#93a6b4"};
inline const QColor accent{"#81d9bc"};     // headings, selection, CPU-side paths
inline const QColor changed{"#ffe19a"};    // values that changed since the last snapshot
inline const QColor changedBack{"#57472c"};
inline const QColor write{"#f2a65a"};      // CPU write attempts
inline const QColor execute{"#5ab4f2"};    // opcode execution
inline const QColor highlight{"#ff5fa2"};  // lesson focus / changed pixels
QString stylesheet();
QFont monospace();
inline QString q(const std::string& s) { return QString::fromStdString(s); }
} // namespace observatory::style
