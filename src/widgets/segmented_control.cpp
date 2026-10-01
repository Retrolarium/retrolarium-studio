#include "widgets/segmented_control.hpp"

#include "theme/icons.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPainter>
#include <QToolButton>
#include <QVariant>

namespace rl::ui {

SegmentedControl::SegmentedControl(QWidget* parent)
    : QWidget(parent), layout_(new QHBoxLayout(this)), group_(new QButtonGroup(this)) {
    // 1px margins hold the outer border; 1px spacing leaves room for dividers.
    layout_->setContentsMargins(1, 1, 1, 1);
    layout_->setSpacing(1);
    group_->setExclusive(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(group_, &QButtonGroup::idClicked, this, [this](int index) {
        emit currentChanged(index, idAt(index));
    });
    connect(&theme::Theme::instance(), &theme::Theme::changed, this, [this] { update(); });
}

QToolButton* SegmentedControl::makeButton(const QString& id) {
    auto* button = new QToolButton(this);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setProperty("variant", QStringLiteral("segment"));
    button->setFocusPolicy(Qt::TabFocus);
    const int index = buttons_.size();
    group_->addButton(button, index);
    layout_->addWidget(button);
    buttons_.push_back(button);
    ids_.push_back(id);
    if (index == 0) {
        button->setChecked(true);
    }
    return button;
}

int SegmentedControl::addSegment(const QString& text, const QString& id) {
    QToolButton* button = makeButton(id.isEmpty() ? text : id);
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setFont(theme::font(theme::TextRole::Body, 12.0));
    button->setFixedHeight(26);
    return buttons_.size() - 1;
}

int SegmentedControl::addIconSegment(const QString& iconName, const QString& toolTip,
                                     const QString& id) {
    QToolButton* button = makeButton(id.isEmpty() ? iconName : id);
    button->setProperty("variant", QStringLiteral("segmentIcon"));
    button->setIcon(theme::icon(iconName, theme::IconTone::Muted));
    button->setIconSize(QSize(15, 15));
    button->setToolTip(toolTip);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setFixedSize(28, 26);
    return buttons_.size() - 1;
}

void SegmentedControl::clear() {
    for (QToolButton* button : buttons_) {
        group_->removeButton(button);
        delete button;
    }
    buttons_.clear();
    ids_.clear();
}

int SegmentedControl::currentIndex() const {
    return group_->checkedId();
}

QString SegmentedControl::currentId() const {
    return idAt(currentIndex());
}

QString SegmentedControl::idAt(int index) const {
    return index >= 0 && index < ids_.size() ? ids_.at(index) : QString();
}

void SegmentedControl::setCurrentIndex(int index) {
    if (index < 0 || index >= buttons_.size() || index == currentIndex()) {
        return;
    }
    buttons_.at(index)->setChecked(true);
    emit currentChanged(index, idAt(index));
}

void SegmentedControl::setCurrentId(const QString& id) {
    setCurrentIndex(ids_.indexOf(id));
}

void SegmentedControl::paintEvent(QPaintEvent*) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    const QRect box = rect();
    painter.setPen(Qt::NoPen);
    // Outer border in the strong line, inner dividers in the hairline.
    painter.fillRect(QRect(box.left(), box.top(), box.width(), 1), t.lineStrong);
    painter.fillRect(QRect(box.left(), box.bottom(), box.width(), 1), t.lineStrong);
    painter.fillRect(QRect(box.left(), box.top(), 1, box.height()), t.lineStrong);
    painter.fillRect(QRect(box.right(), box.top(), 1, box.height()), t.lineStrong);
    for (int i = 1; i < buttons_.size(); ++i) {
        const QRect previous = buttons_.at(i - 1)->geometry();
        painter.fillRect(QRect(previous.right() + 1, box.top() + 1, 1, box.height() - 2), t.line);
    }
}

}  // namespace rl::ui
