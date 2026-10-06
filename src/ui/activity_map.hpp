#pragma once
#include "emulator/state.hpp"
#include "teaching/annotations.hpp"
#include <QWidget>
#include <optional>

class QComboBox;
class QLabel;

namespace observatory {
// Whole-address-space heat map (one cell per address, one row per 256-byte page)
// plus a magnified 16x16 view of a selected page. Counts are real per-address
// CPU write attempts and opcode starts over the map's labelled interval.
class ActivityMapView : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Writes, Executions, Both };
    explicit ActivityMapView(QWidget* parent = nullptr);
    void setMap(const ActivityMap& map, Program program);
    void setMode(Mode mode);
    void setSelectedPage(int page);
    void setSelectedAddress(std::uint16_t address);
    int selectedPage() const { return page_; }
    QSize minimumSizeHint() const override { return {440, 300}; }
signals:
    void addressActivated(std::uint16_t address);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent*) override;
private:
    struct Geometry { QRect overview, detail; int cell; };
    Geometry geometry() const;
    std::optional<std::uint16_t> overviewAddress(QPoint point) const;
    std::optional<std::uint16_t> detailAddress(QPoint point) const;
    QColor cellColor(std::uint16_t address) const;
    void rebuild();
    ActivityMap map_;
    QImage overview_;
    std::vector<std::pair<std::uint16_t, QRgb>> hot_; // Active addresses, drawn at a visible minimum size.
    Mode mode_ = Mode::Both;
    int page_ = 0xC0;
    std::uint16_t selected_ = 0xC000;
    std::optional<std::uint16_t> hover_;
    double logMaxWrites_ = 0, logMaxExecutions_ = 0;
    Program program_ = Program::Other;
};

class ActivityPanel : public QWidget {
    Q_OBJECT
public:
    explicit ActivityPanel(QWidget* parent = nullptr);
    void setMap(const ActivityMap& map, Program program);
    ActivityMapView* view() const { return view_; }
signals:
    void clearRequested();
private:
    ActivityMapView* view_{};
    QComboBox* mode_{};
    QLabel* interval_{};
};
}
