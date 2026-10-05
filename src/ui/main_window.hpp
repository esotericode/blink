#pragma once
#include "emulator/engine.hpp"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>
#include <optional>

class QLabel;
class QTableWidget;
class QComboBox;
class QLineEdit;
class QSplitter;
class QAction;
class QCheckBox;

namespace observatory {
class GameView;
class MainWindow : public QMainWindow {
public:
    explicit MainWindow(std::size_t traceCapacity = 4096);
    const Snapshot& displayedSnapshot() const { return snapshot_; }
    bool running() const { return running_; }
    void run();
    void pause();
    void instructionStep();
    void frameStep();
    void refresh();
    void loadFile(const QString& path);
    void setMemoryBase(std::uint16_t base);
    std::uint16_t selectedAddress() const { return selectedAddress_; }
    const Engine& engine() const { return engine_; }
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    void buildUi();
    void tick();
    void restart();
    void warmTeaching();
    void restoreLayout();
    void updateWriter();
    void updateSelection();
    void inspectMovement();
    Engine engine_;
    Snapshot snapshot_;
    std::optional<Snapshot> previous_;
    bool running_{};
    std::uint16_t memoryBase_ = 0xC000, selectedAddress_ = 0xC000;
    std::optional<std::uint64_t> selectedEvent_;
    QTimer timer_;
    QElapsedTimer wall_, published_;
    std::uint64_t runStartTick_{};
    QAction *runAction_{}, *stepAction_{}, *frameAction_{};
    GameView* game_{};
    QLabel *cursor_{}, *frameLabel_{}, *position_{}, *lesson_{}, *instruction_{}, *hardware_{}, *writer_{}, *activity_{}, *traceStatus_{};
    QTableWidget *registers_{}, *memory_{}, *writes_{};
    QSplitter *outer_{}, *inspectors_{};
    QComboBox* region_{};
    QLineEdit* address_{};
    QCheckBox* trace_{};
};
}
