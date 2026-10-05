#include "ui/main_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QMessageBox>
#include <QStyleFactory>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Console Observatory");
    QApplication::setApplicationVersion("0.1.0");
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setFont(QFont("DejaVu Sans", 10));
    QCommandLineParser parser;
    parser.setApplicationDescription("Native DMG teaching lab; starts offline with its bundled original ROM.");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addPositionalArgument("rom", "Optional 32 KiB ROM-only DMG cartridge.");
    parser.addOption({"trace-capacity", "Bounded write history: 8 to 65536 records (default 4096).", "records", "4096"});
    parser.process(app);
    bool ok = false; auto capacity = parser.value("trace-capacity").toUInt(&ok);
    if (!ok || capacity < 8 || capacity > 65536) parser.showHelp(1);
    observatory::MainWindow window(capacity);
    window.show();
    if (!parser.positionalArguments().isEmpty()) window.loadFile(parser.positionalArguments().first());
    return app.exec();
}
