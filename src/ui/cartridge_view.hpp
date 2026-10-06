#pragma once
#include "emulator/state.hpp"
#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace observatory {
// The CPU's cartridge windows on the left, every ROM/RAM bank the cartridge holds
// on the right, and connectors showing which bank each window shows right now.
// Bank shading is real opcode-start counts per bank over the map's interval.
class BankMap : public QWidget {
    Q_OBJECT
public:
    explicit BankMap(QWidget* parent = nullptr);
    void setData(const CartridgeState& cartridge, const ActivityMap& activity);
    QSize minimumSizeHint() const override { return {420, 168}; }
    QSize sizeHint() const override { return {700, 190}; }
signals:
    void windowActivated(std::uint16_t address);
    void bankSelected(int bank, bool ram);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;
private:
    struct Layout {
        QRectF rom0Window, romWindow, ramWindow, romArea, ramArea;
        int columns{}, ramColumns{};
        double cell{}, ramCell{};
    };
    Layout layout() const;
    QRectF romCell(const Layout& l, int bank) const;
    QRectF ramCell(const Layout& l, int bank) const;
    int romBanks() const;
    int ramBanks() const;
    CartridgeState cartridge_;
    std::vector<std::uint32_t> executions_;
    std::uint64_t bootExecutions_{};
    double logMax_{};
};

class CartridgePanel : public QWidget {
    Q_OBJECT
public:
    explicit CartridgePanel(QWidget* parent = nullptr);
    void setSnapshot(const Snapshot& snapshot, const ActivityMap& activity);
    BankMap* map() const { return map_; }
signals:
    void runUntilBankChange();
    void addressActivated(std::uint16_t address);
private:
    QLabel *facts_{}, *notes_{}, *explanation_{}, *stats_{};
    BankMap* map_{};
    QTableWidget* switches_{};
    QPushButton* run_{};
};
// Plain description of a controller write's effect from its before/after mapping.
QString bankEffect(const WriteEvent& write, const CartridgeInfo& info);
}
