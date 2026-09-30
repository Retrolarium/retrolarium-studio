#include "widgets/primary_button.hpp"

#include "theme/icons.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include <QFontMetrics>
#include <QPainter>

namespace rl::ui {

namespace {
constexpr int kMarkMargin = 4;   // marks sit 4px outside the face
constexpr int kMarkLength = 7;
constexpr int kFaceHeight = 28;
constexpr int kPaddingX = 14;
constexpr int kIconSize = 12;
constexpr int kGap = 7;
}  // namespace

PrimaryButton::PrimaryButton(const QString& text, const QString& iconName, QWidget* parent)
    : QAbstractButton(parent), iconName_(iconName) {
    setText(text);
    setFont(theme::font(theme::TextRole::BodyMedium, 12.5));
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_Hover);
    connect(&theme::Theme::instance(), &theme::Theme::changed, this,
            [this] { update(); });
}

void PrimaryButton::setIconName(const QString& name) {
    iconName_ = name;
    updateGeometry();
    update();
}

QSize PrimaryButton::sizeHint() const {
    const QFontMetrics metrics(font());
    int width = kPaddingX * 2 + metrics.horizontalAdvance(text());
    if (!iconName_.isEmpty()) {
        width += kIconSize + kGap;
    }
    return QSize(width + kMarkMargin * 2, kFaceHeight + kMarkMargin * 2);
}

QSize PrimaryButton::minimumSizeHint() const {
    return sizeHint();
}

QRect PrimaryButton::faceRect() const {
    return rect().adjusted(kMarkMargin, kMarkMargin, -kMarkMargin, -kMarkMargin);
}

bool PrimaryButton::hitButton(const QPoint& pos) const {
    return faceRect().contains(pos);
}

void PrimaryButton::enterEvent(QEvent* event) {
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void PrimaryButton::leaveEvent(QEvent* event) {
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void PrimaryButton::paintEvent(QPaintEvent*) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    const QRect face = faceRect();

    QColor fill = t.accent;
    QColor ink = t.onAccent;
    if (!isEnabled()) {
        fill = t.lineStrong;
        ink = t.faint;
    } else if (isDown() || hovered_) {
        fill = t.accentInk;
    }
    painter.fillRect(face, fill);
    if (hasFocus()) {
        theme::paint::hairlineRect(painter, face.adjusted(-2, -2, 2, 2), t.accent);
    }
    theme::paint::cornerMarks(painter, face, kMarkLength, kMarkMargin, t.mark,
                              theme::paint::kDiagonal);

    const QFontMetrics metrics(font());
    const int textWidth = metrics.horizontalAdvance(text());
    const int contentWidth = textWidth + (iconName_.isEmpty() ? 0 : kIconSize + kGap);
    int x = face.left() + (face.width() - contentWidth) / 2;
    if (!iconName_.isEmpty()) {
        const QPixmap glyph =
            theme::iconPixmap(iconName_, kIconSize, ink, devicePixelRatioF());
        painter.drawPixmap(x, face.top() + (face.height() - kIconSize) / 2, glyph);
        x += kIconSize + kGap;
    }
    painter.setPen(ink);
    painter.setFont(font());
    painter.drawText(QRect(x, face.top(), textWidth + 1, face.height()),
                     Qt::AlignVCenter | Qt::AlignLeft, text());
}

}  // namespace rl::ui
