#include "widgets/rail_button.hpp"

#include "theme/icons.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include <QFontMetrics>
#include <QPainter>

namespace rl::ui {

namespace {
constexpr int kIconSize = 19;
constexpr int kPadTop = 7;
constexpr int kPadBottom = 6;
constexpr int kGap = 3;
constexpr int kBarInset = 6;

QFont labelFont() {
    QFont f = theme::font(theme::TextRole::Body, theme::size::kRail);
    theme::setLetterSpacingEm(f, 0.02);
    return f;
}
}  // namespace

RailButton::RailButton(const QString& iconName, const QString& label, QWidget* parent)
    : QAbstractButton(parent), iconName_(iconName) {
    setText(label);
    setToolTip(label);
    setCheckable(true);
    setAutoExclusive(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(&theme::Theme::instance(), &theme::Theme::changed, this, [this] { update(); });
}

QSize RailButton::sizeHint() const {
    const QFontMetrics metrics(labelFont());
    return QSize(62, kPadTop + kIconSize + kGap + metrics.height() + kPadBottom);
}

QSize RailButton::minimumSizeHint() const { return sizeHint(); }

void RailButton::enterEvent(QEvent* event) {
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void RailButton::leaveEvent(QEvent* event) {
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void RailButton::paintEvent(QPaintEvent*) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    const bool on = isChecked();
    if (on) {
        painter.fillRect(rect(), t.hover);
        painter.fillRect(QRect(0, kBarInset, 2, height() - kBarInset * 2), t.accent);
    }
    const QColor ink = on || hovered_ || hasFocus() ? t.text : t.muted;
    const QPixmap glyph = theme::iconPixmap(iconName_, kIconSize, ink, devicePixelRatioF());
    painter.drawPixmap((width() - kIconSize) / 2, kPadTop, glyph);
    painter.setFont(labelFont());
    painter.setPen(ink);
    painter.drawText(QRect(0, kPadTop + kIconSize + kGap, width(), height()), Qt::AlignHCenter | Qt::AlignTop, text());
}

}  // namespace rl::ui
