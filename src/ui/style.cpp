#include "ui/style.hpp"
#include <QFontDatabase>

namespace observatory::style {
QString stylesheet() {
    return R"(
        QMainWindow, QWidget { background: #111b24; color: #dce4ea; }
        QMainWindow::separator { background: #283944; width: 5px; height: 5px; }
        QMenuBar { background: #0d151c; border-bottom: 1px solid #34424e; }
        QMenuBar::item { padding: 5px 10px; background: transparent; }
        QMenuBar::item:selected, QMenu::item:selected { background: #335c68; }
        QMenu { background: #17232d; border: 1px solid #425866; }
        QMenu::item { padding: 6px 24px 6px 20px; }
        QMenu::item:disabled { color: #657682; }
        QMenu::separator { height: 1px; background: #34424e; margin: 4px 8px; }
        QDockWidget { color: #81d9bc; font-weight: 600; }
        QDockWidget::title { background: #17232d; padding: 6px 8px; border-bottom: 1px solid #34424e; }
        QTabBar::tab { background: #17232d; color: #b5c7d2; padding: 6px 14px; border: 1px solid #34424e; border-bottom: 0; }
        QTabBar::tab:selected { background: #243441; color: #81d9bc; }
        QToolBar { spacing: 6px; border-bottom: 1px solid #34424e; padding: 6px; }
        QToolButton, QPushButton { background: #253745; border: 1px solid #425866; border-radius: 4px; padding: 6px 11px; }
        QToolButton:hover, QPushButton:hover { background: #355361; }
        QToolButton:checked { background: #2c6b5c; border-color: #81d9bc; color: #e9fff6; }
        QToolButton:disabled, QPushButton:disabled { color: #657682; border-color: #34424e; }
        QPushButton#primaryAction { background: #2c6b5c; border-color: #81d9bc; }
        QPushButton#primaryAction:hover { background: #378270; }
        QTableWidget { background: #17232d; border: 1px solid #34424e; selection-background-color: #335c68; gridline-color: #22313c; }
        QHeaderView::section { background: #243441; color: #bacbd7; border: 0; padding: 4px; }
        QLineEdit, QComboBox { background: #243441; border: 1px solid #425866; border-radius: 3px; padding: 4px; }
        QComboBox QAbstractItemView { background: #17232d; selection-background-color: #335c68; }
        QCheckBox::indicator { width: 14px; height: 14px; }
        QScrollBar:vertical { background: #17232d; width: 12px; }
        QScrollBar::handle:vertical { background: #34424e; min-height: 24px; border-radius: 4px; }
        QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
        QStatusBar { color: #b5c7d2; border-top: 1px solid #34424e; }
        QLabel#stateBadge { border-radius: 4px; padding: 3px 8px; font-weight: 700; }
        QLabel#hint { color: #93a6b4; }
        QLabel#cursorLabel { color: #b5c7d2; font-size: 9pt; }
    )";
}
QFont monospace() {
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#if defined(Q_OS_WIN)
    font.setFamily("Consolas"); // Qt's Windows fixed-font default is Courier New.
#endif
    return font;
}
} // namespace observatory::style
