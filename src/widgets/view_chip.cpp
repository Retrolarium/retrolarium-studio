#include "widgets/view_chip.hpp"

#include "sdk/i18n.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include <QFontMetrics>
#include <QMenu>
#include <QPainter>

namespace rl::ui {

namespace {
constexpr int kHeight = 26;
constexpr int kPaddingX = 9;
constexpr int kGap = 6;
constexpr double kTextPx = 12.0;

QFont keyFont() {
    return theme::font(theme::TextRole::Body, kTextPx);
}
QFont valueFont() {
    return theme::font(theme::TextRole::BodyMedium, kTextPx);
}
}  // namespace

ViewChip::ViewChip(const QString& key, const QString& value, QWidget* parent)
    : QAbstractButton(parent), key_(key), value_(value) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    connect(this, &QAbstractButton::clicked, this, [this] {
        if (!choices_.isEmpty()) {
            showChoices();
        }
    });
    connect(&theme::Theme::instance(), &theme::Theme::changed, this, [this] { update(); });
}

void ViewChip::setKey(const QString& key) {
    key_ = key;
    updateGeometry();
    update();
}

void ViewChip::setValue(const QString& value) {
    value_ = value;
    updateGeometry();
    update();
}

void ViewChip::setChoices(const QStringList& choices) {
    choices_ = choices;
}

void ViewChip::showChoices() {
    QMenu menu(this);
    for (const QString& choice : choices_) {
        QAction* action = menu.addAction(rlValue(choice));
        action->setData(choice);
        action->setCheckable(true);
        action->setChecked(choice == value_);
    }
    QAction* picked = menu.exec(mapToGlobal(QPoint(0, height() + 2)));
    if (picked && picked->data().toString() != value_) {
        setValue(picked->data().toString());
        emit valueChosen(value_);
    }
}

QSize ViewChip::sizeHint() const {
    const int width = kPaddingX * 2 + QFontMetrics(keyFont()).horizontalAdvance(key_) + kGap +
                      QFontMetrics(valueFont()).horizontalAdvance(rlValue(value_));
    return QSize(width, kHeight);
}

QSize ViewChip::minimumSizeHint() const {
    return sizeHint();
}

void ViewChip::enterEvent(QEvent* event) {
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ViewChip::leaveEvent(QEvent* event) {
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ViewChip::paintEvent(QPaintEvent*) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    const QRect box = rect();
    const bool active = hovered_ || isDown() || hasFocus();
    if (active) {
        painter.fillRect(box, t.hover);
    }
    theme::paint::hairlineRect(painter, box, active ? t.lineStrong : t.line);

    const QFont kf = keyFont();
    const QFont vf = valueFont();
    const int keyWidth = QFontMetrics(kf).horizontalAdvance(key_);
    painter.setFont(kf);
    painter.setPen(t.muted);
    painter.drawText(QRect(kPaddingX, 0, keyWidth + 1, box.height()), Qt::AlignVCenter, key_);
    painter.setFont(vf);
    painter.setPen(isChecked() || !isCheckable() ? t.text : t.muted);
    painter.drawText(QRect(kPaddingX + keyWidth + kGap, 0, box.width(), box.height()),
                     Qt::AlignVCenter, rlValue(value_));
}

}  // namespace rl::ui
