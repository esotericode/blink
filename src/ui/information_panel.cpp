#include "ui/information_panel.hpp"
#include "teaching/annotations.hpp"
#include "teaching/glossary.hpp"
#include "teaching/information.hpp"
#include "ui/style.hpp"
#include "ui/tooltip.hpp"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace observatory {
using style::q;
InformationPanel::InformationPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("informationPanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 6, 10, 8);
    root->setSpacing(5);
    auto* navigation = new QHBoxLayout;
    back_ = new QPushButton("Back"); back_->setObjectName("informationBack");
    forward_ = new QPushButton("Forward"); forward_->setObjectName("informationForward");
    back_->setToolTip(tips::key("info.back")); forward_->setToolTip(tips::key("info.forward"));
    navigation->addWidget(back_); navigation->addWidget(forward_);
    topics_ = new QComboBox; topics_->setObjectName("informationTopics");
    topics_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    topics_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    topics_->setMinimumContentsLength(12);
    topics_->setToolTip(tips::key("info.topics"));
    topics_->setAccessibleName("Browse a hardware topic");
    for (const auto& article : informationArticles()) topics_->addItem(q(article.title), q(article.id));
    navigation->addWidget(topics_, 1);
    root->addLayout(navigation);
    heading_ = new QLabel; heading_->setObjectName("informationHeading"); heading_->setWordWrap(true);
    auto font = heading_->font(); font.setBold(true); font.setPointSizeF(font.pointSizeF() + 2); heading_->setFont(font);
    root->addWidget(heading_);
    facts_ = new QLabel; facts_->setObjectName("informationFacts"); facts_->setWordWrap(true);
    facts_->setTextFormat(Qt::PlainText); facts_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    facts_->setStyleSheet(QString("color:%1;").arg(style::muted.name()));
    root->addWidget(facts_);
    body_ = new QTextBrowser; body_->setObjectName("informationBody");
    body_->setFrameShape(QFrame::NoFrame);
    body_->setOpenLinks(false); body_->setOpenExternalLinks(false);
    body_->setAccessibleName("Detailed hardware explanation");
    body_->document()->setDefaultStyleSheet(QString("p { margin-top:4px; margin-bottom:10px; } h3 { color:%1; margin-top:12px; margin-bottom:4px; } a { color:%1; }").arg(style::accent.name()));
    root->addWidget(body_, 1);
    connect(back_, &QPushButton::clicked, this, [this] { moveHistory(-1); });
    connect(forward_, &QPushButton::clicked, this, [this] { moveHistory(1); });
    connect(topics_, qOverload<int>(&QComboBox::activated), this, [this](int i) { showTopic(topics_->itemData(i).toString().toStdString()); });
    connect(body_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        const auto link = url.toString();
        if (link.startsWith("topic:")) showTopic(link.mid(6).toStdString());
        else if (link.startsWith("addr:")) {
            bool ok = false; const auto a = link.mid(5).toUInt(&ok, 16);
            if (ok && a <= 0xFFFF) emit addressActivated(std::uint16_t(a));
        }
    });
    reset();
}
void InformationPanel::reset() {
    history_.clear(); position_ = -1; rendered_.clear();
    select({});
}
void InformationPanel::showTopic(const std::string& topic) {
    InformationSelection selection; selection.topic = topic; select(selection);
}
void InformationPanel::select(const InformationSelection& selection) {
    if (!informationArticle(selection.topic)) return;
    if (position_ >= 0 && current_ == selection) { updateFacts(); return; }
    if (position_ + 1 < int(history_.size())) history_.resize(std::size_t(position_ + 1));
    history_.push_back(selection);
    if (history_.size() > 64) history_.erase(history_.begin());
    position_ = int(history_.size()) - 1;
    current_ = selection; display();
}
void InformationPanel::moveHistory(int direction) {
    const auto next = position_ + direction;
    if (next < 0 || next >= int(history_.size())) return;
    position_ = next; current_ = history_[std::size_t(next)]; display();
}
void InformationPanel::setSnapshot(const Snapshot& snapshot) { snapshot_ = snapshot; updateFacts(); }
void InformationPanel::display() {
    const auto* article = informationArticle(current_.topic); if (!article) return;
    heading_->setText(q(article->title));
    const QSignalBlocker blocker(topics_);
    topics_->setCurrentIndex(topics_->findData(q(article->id)));
    QString html;
    if (!current_.glossaryKey.empty()) {
        const auto& brief = glossary(current_.glossaryKey);
        if (!brief.empty()) html += "<p><b>" + q(brief.title).toHtmlEscaped() + "</b><br>" + q(brief.body).toHtmlEscaped() + "</p>";
    } else if (current_.kind == InformationKind::Memory) {
        const auto brief = ioRegisterExplanation(current_.address);
        if (!brief.empty()) html += "<p><b>" + q(brief.title).toHtmlEscaped() + "</b><br>" + q(brief.body).toHtmlEscaped() + "</p>";
    }
    html += "<p>" + q(article->summary).toHtmlEscaped() + "</p><h3>How it works</h3>" + q(article->how) +
            "<h3>How it fits into a game</h3>" + q(article->uses) + "<h3>Reading this inspector</h3>" + q(article->limits);
    if (current_.kind == InformationKind::Tile || current_.kind == InformationKind::Sprite || current_.kind == InformationKind::MapCell) {
        const auto a = current_.kind == InformationKind::Tile ? tileAddress(current_.index) : current_.kind == InformationKind::Sprite ?
            std::uint16_t(0xFE00 + current_.index * 4) : current_.address;
        html += QString("<p><a href='addr:%1'>Open selected storage in Memory (%2)</a></p>").arg(a, 4, 16, QChar('0')).arg(q(hex(a)));
    }
    html += "<h3>Related topics</h3><p>";
    for (const auto& id : article->related) if (const auto* related = informationArticle(id))
        html += "<a href='topic:" + q(id) + "'>" + q(related->title).toHtmlEscaped() + "</a><br>";
    html += "</p>";
    // Refreshing live facts, or selecting another byte in the same region, must
    // not throw the reader back to the beginning of an unchanged article.
    if (html != rendered_) { rendered_ = html; body_->setHtml(html); }
    back_->setEnabled(position_ > 0); forward_->setEnabled(position_ + 1 < int(history_.size()));
    updateFacts();
}
void InformationPanel::updateFacts() {
    const auto& s = snapshot_;
    QString facts;
    switch (current_.kind) {
    case InformationKind::Topic:
        facts = "General hardware reference · click an item to attach its observed facts."; break;
    case InformationKind::Memory: {
        const auto a = current_.address;
        facts = "Selected " + q(hex(a));
        const auto name = addressName(a, programOf(s)); if (!name.empty()) facts += " · " + q(name);
        if (a >= s.memoryBase && std::size_t(a - s.memoryBase) < memoryWindow) {
            const auto i = std::size_t(a - s.memoryBase);
            facts += s.memoryAvailable[i] ? QString(" · storage %1 (%2)").arg(q(hex(s.memory[i], 2))).arg(s.memory[i]) : " · no storage value";
        } else facts += " · open in Memory for the current byte";
        if (a < 0x8000) facts += QString(" · mapped ROM bank %1").arg(a < 0x4000 ? s.cartridge.banks.rom0 : s.cartridge.banks.rom);
        if (a >= 0xA000 && a < 0xC000) facts += QString(" · mapped RAM bank %1").arg(s.cartridge.banks.ram);
        if (a >= 0xE000 && a < 0xFE00) facts += " · mirrors " + q(hex(a - 0x2000));
        break;
    }
    case InformationKind::Register: {
        const std::array<std::uint16_t, 6> values{s.registers.af, s.registers.bc, s.registers.de, s.registers.hl, s.registers.sp, s.registers.pc};
        const char* names[] = {"AF", "BC", "DE", "HL", "SP", "PC"};
        const auto i = std::clamp(current_.index, 0, 5); const auto v = values[std::size_t(i)];
        facts = QString("Selected %1 = %2 (%3)").arg(names[i], q(hex(v))).arg(v);
        if (i < 4) facts += QString(" · high byte %1, low byte %2").arg(q(hex(v >> 8, 2)), q(hex(v & 0xFF, 2)));
        break;
    }
    case InformationKind::Flag: {
        const auto i = std::clamp(current_.index, 0, 3);
        facts = QString("Selected %1 flag = %2 · F = %3").arg(QChar("ZNHC"[i])).arg(bool(s.registers.af & (0x80 >> i))).arg(q(hex(s.registers.af & 0xFF, 2))); break;
    }
    case InformationKind::Tile:
        facts = QString("Selected physical tile %1 · %2–%3 · 16 bytes, 8×8 pixels")
            .arg(current_.index).arg(q(hex(tileAddress(current_.index))), q(hex(tileAddress(current_.index) + 15))); break;
    case InformationKind::Sprite: {
        const auto index = std::clamp(current_.index, 0, spriteCount - 1); const auto object = sprite(s.video.oam, index);
        const auto height = s.video.lcdc & 4 ? 16 : 8;
        facts = QString("Selected sprite %1 · OAM %2 · screen (%3, %4), 8×%5 · tile %6 · palette OBP%7%8%9")
            .arg(index).arg(q(hex(0xFE00 + index * 4))).arg(object.screenX()).arg(object.screenY()).arg(height)
            .arg(height == 16 ? object.tile & 0xFE : object.tile).arg(object.palette())
            .arg(object.flipX() ? " · X flip" : "").arg(object.flipY() ? " · Y flip" : ""); break;
    }
    case InformationKind::MapCell: {
        const auto a = std::clamp<int>(current_.address, 0x9800, 0x9FFF);
        const auto entry = s.video.vram[std::size_t(a - 0x8000)]; const int tile = backgroundTile(entry, s.video.lcdc & 0x10);
        facts = QString("Map %1 cell (%2, %3): %5 = %4 → tile %6 (%7) · %8")
            .arg(q(hex(a & 0xFC00))).arg((a & 0x3FF) % 32).arg((a & 0x3FF) / 32).arg(q(hex(entry, 2)), q(hex(a)))
            .arg(tile).arg(q(hex(tileAddress(tile))), s.video.lcdc & 0x10 ? "unsigned $8000" : "signed $9000"); break;
    }
    case InformationKind::Write: {
        const auto it = std::find_if(s.writes.begin(), s.writes.end(), [this](const auto& e) { return e.id == current_.event; });
        if (it == s.writes.end()) facts = QString("Selected write #%1 · no longer retained; this is incomplete history, not a replay").arg(current_.event);
        else facts = QString("Selected CPU attempt #%1 · %2 ← %3 · retained interval [%4, %5] ticks%6")
            .arg(it->id).arg(q(hex(it->address)), q(hex(it->requested, 2))).arg(it->startTicks).arg(it->endTicks)
            .arg(it->instruction ? " · " + q(disassemble(*it->instruction)) : " · no opcode identity"); break;
    }
    case InformationKind::Bank:
        facts = QString("Selected %1 bank %2 · %3").arg(current_.ram ? "RAM" : "ROM").arg(current_.index)
            .arg(current_.ram ? (current_.index == s.cartridge.banks.ram ? "currently selected at $A000" : "currently unmapped") :
                 current_.index == s.cartridge.banks.rom ? "currently mapped at $4000" : current_.index == s.cartridge.banks.rom0 ? "currently mapped at $0000" : "currently unmapped"); break;
    }
    if (current_.kind != InformationKind::Topic) facts += QString("\nState now: t = %1 ticks").arg(s.ticks);
    facts_->setText(facts);
}
} // namespace observatory
