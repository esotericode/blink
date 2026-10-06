#pragma once
#include "emulator/state.hpp"
#include <QWidget>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QTextBrowser;

namespace observatory {
enum class InformationKind { Topic, Memory, Register, Flag, Tile, Sprite, MapCell, Write, Bank };
struct InformationSelection {
    InformationKind kind = InformationKind::Topic;
    std::string topic = "explore", glossaryKey;
    std::uint16_t address{};
    int index{};
    std::uint64_t event{};
    bool ram{};
    bool operator==(const InformationSelection&) const = default;
};
// The article stays still during live execution. Only the small facts label is
// refreshed, from the same snapshot as every other inspector.
class InformationPanel : public QWidget {
    Q_OBJECT
public:
    explicit InformationPanel(QWidget* parent = nullptr);
    void setSnapshot(const Snapshot& snapshot);
    void select(const InformationSelection& selection);
    void showTopic(const std::string& topic);
    void reset();
    const InformationSelection& selection() const { return current_; }
signals:
    void addressActivated(std::uint16_t address);
private:
    void display();
    void updateFacts();
    void moveHistory(int direction);
    InformationSelection current_;
    std::vector<InformationSelection> history_;
    int position_ = -1;
    Snapshot snapshot_;
    QString rendered_;
    QLabel *heading_{}, *facts_{};
    QTextBrowser* body_{};
    QComboBox* topics_{};
    QPushButton *back_{}, *forward_{};
};
} // namespace observatory
