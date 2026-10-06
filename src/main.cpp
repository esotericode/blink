#include "ui/main_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QMessageBox>
#include <QStyleFactory>
#include <QTimer>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Console Observatory");
    QApplication::setApplicationVersion(OBSERVATORY_VERSION);
    app.setStyle(QStyleFactory::create("Fusion"));
#if defined(Q_OS_LINUX)
    // Consistent metrics on the reference platform; Windows/macOS keep their UI font.
    app.setFont(QFont("DejaVu Sans", 10));
#endif
    QCommandLineParser parser;
    parser.setApplicationDescription("Native DMG teaching lab; starts offline with its bundled original ROM.\n"
                                     "Battery saves are read from and written to <rom name>.sav beside the ROM.");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addPositionalArgument("rom", "Optional Game Boy ROM file to open (any SameBoy-supported cartridge, up to 8 MiB).");
    parser.addOption({"trace-capacity", "Bounded write history: 8 to 65536 records (default 4096).", "records", "4096"});
    parser.addOption({"screenshot", "Save the initial window to an image file, then exit (packaging smoke test).", "file"});
    parser.process(app);
    bool ok = false; auto capacity = parser.value("trace-capacity").toUInt(&ok);
    if (!ok || capacity < 8 || capacity > 65536) parser.showHelp(1);
    observatory::MainWindow window(capacity);
    window.show();
    if (!parser.positionalArguments().isEmpty()) window.loadFile(parser.positionalArguments().first());
    if (parser.isSet("screenshot")) {
        const auto path = parser.value("screenshot");
        QTimer::singleShot(250, &window, [&window, path] {
            QApplication::exit(window.grab().save(path) ? 0 : 2);
        });
    }
    return app.exec();
}
