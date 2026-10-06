#include "widgets/property_grid.hpp"

#include "sdk/i18n.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/pixel_icon.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QPainter>

#include <algorithm>

namespace rl::ui {

namespace {
constexpr int kFieldHeight = 26;
constexpr int kHintGap = 6;
constexpr double kHintPx = 10.0;
constexpr int kChoiceIcon = 20;
// The widest the key column grows before its keys wrap.
constexpr int kMaxKeyWidth = 132;

QFont hintFont() {
    return theme::font(theme::TextRole::Body, kHintPx);
}

void clearLayout(QGridLayout* grid) {
    while (QLayoutItem* item = grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}
}  // namespace

PropertyField::PropertyField(const QString& value, const QString& hint, QWidget* parent)
    : QLineEdit(value, parent), hint_(hint) {
    setFont(theme::font(theme::TextRole::Body, theme::size::kRow));
    setFixedHeight(kFieldHeight);
    updateMargins();
    // A value wider than the field shows its start, not its end.
    setCursorPosition(0);
}

void PropertyField::setHint(const QString& hint) {
    hint_ = hint;
    updateMargins();
    update();
}

void PropertyField::setMonospace(bool monospace) {
    setFont(monospace ? theme::font(theme::TextRole::Mono, 12.0)
                      : theme::font(theme::TextRole::Body, theme::size::kRow));
}

void PropertyField::updateMargins() {
    const int hintWidth =
        hint_.isEmpty() ? 0 : QFontMetrics(hintFont()).horizontalAdvance(hint_) + kHintGap;
    setTextMargins(0, 0, hintWidth, 0);
}

void PropertyField::paintEvent(QPaintEvent* event) {
    QLineEdit::paintEvent(event);
    if (hint_.isEmpty()) {
        return;
    }
    QPainter painter(this);
    painter.setFont(hintFont());
    painter.setPen(theme::current().faint);
    // Padding of the style sheet is 8px; the hint sits inside it.
    painter.drawText(rect().adjusted(0, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight, hint_);
}

PropertyGrid::PropertyGrid(QWidget* parent) : QWidget(parent), grid_(new QGridLayout(this)) {
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(10);
    grid_->setVerticalSpacing(7);
    grid_->setColumnMinimumWidth(0, keyWidth_);
    grid_->setColumnStretch(1, 1);
}

void PropertyGrid::setKeyWidth(int pixels) {
    baseKeyWidth_ = keyWidth_ = pixels;
    grid_->setColumnMinimumWidth(0, keyWidth_);
}

void PropertyGrid::addRow(const QString& key, QWidget* field) {
    auto* label = new Label(key, Label::Role::Row, this);
    label->setTone(QStringLiteral("muted"));
    label->setWordWrap(true);
    const int wanted = std::min(kMaxKeyWidth, label->fontMetrics().horizontalAdvance(key) + 2);
    if (wanted > keyWidth_) {
        keyWidth_ = wanted;
        grid_->setColumnMinimumWidth(0, keyWidth_);
        for (QWidget* earlier : keys_) earlier->setFixedWidth(keyWidth_);
    }
    label->setFixedWidth(keyWidth_);
    keys_ << label;
    grid_->addWidget(label, rows_, 0, Qt::AlignVCenter);
    field->setParent(this);
    grid_->addWidget(field, rows_, 1);
    ++rows_;
}

PropertyField* PropertyGrid::addField(const QString& key, const QString& value,
                                      const QString& hint) {
    auto* field = new PropertyField(value, hint);
    addRow(key, field);
    return field;
}

QComboBox* PropertyGrid::addChoice(const QString& key, const QStringList& choices, int current,
                                   const QVector<QImage>& icons) {
    auto* combo = new QComboBox;
    combo->setFont(theme::font(theme::TextRole::Body, theme::size::kRow));
    combo->setFixedHeight(kFieldHeight);
    // Long choice lists (every sprite mapping) must not widen the inspector;
    // the popup still shows the full names.
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(8);
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Shown translated; the item data keeps the choice as the model gave it.
    for (const QString& choice : choices) combo->addItem(rlValue(choice), choice);
    // Pixel-art previews, scaled by whole steps so they stay crisp.
    bool anyIcon = false;
    for (int i = 0; i < choices.size() && i < icons.size(); ++i) {
        if (icons.at(i).isNull()) continue;
        combo->setItemIcon(i, QIcon(QPixmap::fromImage(pixelIcon(icons.at(i), kChoiceIcon))));
        anyIcon = true;
    }
    if (anyIcon) {
        combo->setIconSize(QSize(kChoiceIcon, kChoiceIcon));
        combo->view()->setIconSize(QSize(kChoiceIcon, kChoiceIcon));
    }
    combo->view()->setMinimumWidth(combo->view()->sizeHintForColumn(0) + 24);
    combo->setCurrentIndex(current);
    addRow(key, combo);
    return combo;
}

void PropertyGrid::clear() {
    clearLayout(grid_);
    rows_ = 0;
    keys_.clear();
    keyWidth_ = baseKeyWidth_;
    grid_->setColumnMinimumWidth(0, keyWidth_);
}

EngineTable::EngineTable(QWidget* parent) : QWidget(parent), grid_(new QGridLayout(this)) {
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(10);
    grid_->setVerticalSpacing(5);
    grid_->setColumnMinimumWidth(0, 100);
    grid_->setColumnStretch(1, 1);
}

void EngineTable::addRow(const QString& key, const QString& value) {
    auto* keyLabel = new Label(key, Label::Role::Mono, this);
    keyLabel->setTone(QStringLiteral("muted"));
    keyLabel->setFixedWidth(100);
    auto* valueLabel = new Label(value, Label::Role::Mono, this);
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grid_->addWidget(keyLabel, rows_, 0);
    grid_->addWidget(valueLabel, rows_, 1);
    ++rows_;
}

void EngineTable::clear() {
    clearLayout(grid_);
    rows_ = 0;
}

}  // namespace rl::ui
