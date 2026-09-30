#include "widgets/budget_bar.hpp"

#include "theme/theme.hpp"

#include <QPainter>

#include <algorithm>

namespace rl::ui {

BudgetBar::BudgetBar(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(80, thickness_);
    connect(&theme::Theme::instance(), &theme::Theme::changed, this, [this] { update(); });
}

void BudgetBar::setBudget(qint64 used, qint64 capacity) {
    used_ = std::max<qint64>(0, used);
    capacity_ = std::max<qint64>(0, capacity);
    update();
}

void BudgetBar::setBarThickness(int pixels) {
    thickness_ = std::max(1, pixels);
    setFixedHeight(thickness_);
    updateGeometry();
}

QSize BudgetBar::sizeHint() const {
    return QSize(80, thickness_);
}

void BudgetBar::paintEvent(QPaintEvent*) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    const QRect track(0, (height() - thickness_) / 2, width(), thickness_);
    painter.fillRect(track, t.lineStrong);
    if (capacity_ <= 0) {
        return;
    }
    if (overBudget()) {
        painter.fillRect(track, t.text);
        return;
    }
    const int fill = static_cast<int>(track.width() * static_cast<double>(used_) / capacity_);
    painter.fillRect(QRect(track.left(), track.top(), fill, track.height()), t.accent);
}

}  // namespace rl::ui
