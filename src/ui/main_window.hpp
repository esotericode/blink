#pragma once
#include "emulator/engine.hpp"
#include "teaching/lesson.hpp"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>
#include <optional>

class QAction;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QTableWidget;

namespace observatory {
class ActivityPanel;
class GameView;
class SystemDiagram;
class TileInspector;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(std::size_t traceCapacity = 4096);
    const Snapshot& displayedSnapshot() const { return snapshot_; }
    bool running() const { return running_; }
    void run();
    void pause();
    void instructionStep();
    void frameStep();
    // Break on write: run until the CPU attempts a write to `address` (or a limit).
    WatchResult runUntilWritten(std::uint16_t address);
    void refresh();
    void loadFile(const QString& path);
    void loadTeaching();
    void setMemoryBase(std::uint16_t base);
    void selectAddress(std::uint16_t address);
    std::uint16_t selectedAddress() const { return selectedAddress_; }
    const Engine& engine() const { return engine_; }
    // Guided lesson: perform the current step's action, or stop and release input.
    void advanceLesson();
    void stopLesson();
    LessonStep lessonStep() const { return lessonStep_; }
    void resetLayout();
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
private:
    void buildActions();
    void buildCentral();
    void buildDocks();
    QDockWidget* makeDock(const QString& title, const QString& name, QWidget* content);
    void tick();
    void restart();
    void warmTeaching();
    void afterLoad();
    void updateWriter();
    void updateSelection();
    void updatePanels(bool force);
    void inspectMovement();
    void setLessonStep(LessonStep step, const LessonEvidence& evidence = {}, const QString& problem = {});
    void showLicenses();
    bool ownsKeyboard(QWidget* widget) const;
    Engine engine_;
    Snapshot snapshot_;
    std::optional<Snapshot> previous_;
    ActivityMap activity_;
    bool running_{}, laidOut_{};
    std::uint16_t memoryBase_ = 0xC000, selectedAddress_ = 0xC000;
    std::optional<std::uint64_t> selectedEvent_;
    LessonStep lessonStep_ = LessonStep::Start;
    QTimer timer_;
    QElapsedTimer wall_, published_;
    std::uint64_t runStartTick_{};
    QAction *runAction_{}, *stepAction_{}, *frameAction_{}, *untilAction_{}, *traceAction_{};
    QAction *spritesAction_{}, *changesAction_{};
    QMenu* viewMenu_{};
    GameView* game_{};
    SystemDiagram* diagram_{};
    TileInspector* tiles_{};
    ActivityPanel* map_{};
    QDockWidget *systemDock_{}, *cpuDock_{}, *memoryDock_{}, *tilesDock_{}, *mapDock_{}, *writesDock_{}, *lessonDock_{};
    QLabel *badge_{}, *cursor_{}, *frameLabel_{}, *instruction_{}, *flags_{}, *writer_{}, *selection_{};
    QLabel *activityLabel_{}, *traceStatus_{}, *lessonProgress_{}, *lessonHeading_{}, *lessonBody_{};
    QPushButton *lessonAction_{}, *lessonStop_{};
    QTableWidget *registers_{}, *memory_{}, *writes_{};
    QComboBox* region_{};
    QLineEdit* address_{};
};
}
