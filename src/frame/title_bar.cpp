#include "frame/title_bar.hpp"

#include "theme/brand.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace rl::frame {

namespace {

constexpr int kButtonWidth = 46;
constexpr int kPaddingLeft = 14;
constexpr int kMarkSize = 16;
constexpr int kTitleGap = 24;

// Windows' own close-button red, so the most destructive button reads the
// same as in every other application.
const QColor kCloseHover(0xc4, 0x2b, 0x1c);
const QColor kClosePressed(0xb2, 0x27, 0x1a);

// The Retrolarium mark, muted while the window is inactive.
class AppMark final : public QWidget {
public:
    explicit AppMark(QWidget* parent) : QWidget(parent) {
        setFixedSize(kMarkSize, kMarkSize);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const QWidget* w = window();
        const bool active = w->isActiveWindow() || w->property("rlDrawActive").toBool();
        QPainter painter(this);
        painter.drawPixmap(0, 0, theme::brand::mark(theme::Theme::instance().mode(), kMarkSize, devicePixelRatioF(),
                                                    !active));
    }
};

}  // namespace

TitleBar::TitleBar(QWidget* window)
    : QWidget(window),
      window_(window),
      layout_(new QHBoxLayout(this)),
      mark_(new AppMark(this)),
      menuBar_(new QMenuBar(this)),
      trailing_(new QWidget(this)),
      trailingLayout_(new QHBoxLayout(trailing_)) {
    setFixedHeight(kHeight);
    setMouseTracking(true);
    setAttribute(Qt::WA_StyledBackground, false);

    menuBar_->setProperty("titlebar", true);
    menuBar_->setNativeMenuBar(false);
    menuBar_->setFont(theme::font(theme::TextRole::Body, 13.0));
    menuBar_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    trailingLayout_->setContentsMargins(0, 0, 6, 0);
    trailingLayout_->setSpacing(2);

    layout_->setContentsMargins(kPaddingLeft, 0, buttonsWidth(), 0);
    layout_->setSpacing(0);
    layout_->addWidget(mark_, 0, Qt::AlignVCenter);
    layout_->addSpacing(10);
    layout_->addWidget(menuBar_, 0, Qt::AlignVCenter);
    layout_->addStretch(1);
    layout_->addWidget(trailing_, 0, Qt::AlignVCenter);

    window_->installEventFilter(this);
    connect(&theme::Theme::instance(), &theme::Theme::changed, this, [this] {
        update();
        mark_->update();
    });
}

void TitleBar::setTitle(const QString& primary, const QString& secondary) {
    primary_ = primary;
    secondary_ = secondary;
    update();
}

void TitleBar::addTrailingWidget(QWidget* widget) {
    trailingLayout_->addWidget(widget);
}

void TitleBar::setWindowButtonsVisible(bool visible) {
    buttonsVisible_ = visible;
    layout_->setContentsMargins(kPaddingLeft, 0, buttonsWidth(), 0);
    update();
}

void TitleBar::setDialogStyle() {
    dialog_ = true;
    menuBar_->hide();
    layout_->setContentsMargins(kPaddingLeft, 0, buttonsWidth(), 0);
    update();
}

int TitleBar::buttonsWidth() const {
    if (!buttonsVisible_) {
        return kPaddingLeft;
    }
    return dialog_ ? kButtonWidth : kButtonWidth * 3;
}

QRect TitleBar::buttonRect(Button button) const {
    if (!buttonsVisible_ || (dialog_ && button != Button::Close)) {
        return QRect();
    }
    const int right = width();
    switch (button) {
    case Button::Close:
        return QRect(right - kButtonWidth, 0, kButtonWidth, height());
    case Button::Maximize:
        return QRect(right - kButtonWidth * 2, 0, kButtonWidth, height());
    case Button::Minimize:
        return QRect(right - kButtonWidth * 3, 0, kButtonWidth, height());
    case Button::None:
        break;
    }
    return QRect();
}

TitleBar::Button TitleBar::buttonAt(const QPoint& pos) const {
    for (Button button : {Button::Minimize, Button::Maximize, Button::Close}) {
        if (buttonRect(button).contains(pos)) {
            return button;
        }
    }
    return Button::None;
}

bool TitleBar::windowActive() const {
    // Off-screen renders are never active; they may ask to be drawn as if.
    return window_->isActiveWindow() || window_->property("rlDrawActive").toBool();
}

bool TitleBar::isDragArea(const QPoint& pos) const {
    if (!rect().contains(pos) || buttonAt(pos) != Button::None) {
        return false;
    }
    QWidget* child = childAt(pos);
    if (!child || child == mark_) {
        return true;
    }
    if (child == menuBar_) {
        // The empty stretch of the menu bar moves the window; items do not.
        return menuBar_->actionAt(menuBar_->mapFrom(this, pos)) == nullptr;
    }
    return child == trailing_;
}

void TitleBar::setNativeHover(Button button) {
    if (hovered_ != button) {
        hovered_ = button;
        update();
    }
}

void TitleBar::setNativePressed(Button button) {
    if (pressed_ != button) {
        pressed_ = button;
        update();
    }
}

void TitleBar::trigger(Button button) {
    switch (button) {
    case Button::Minimize:
        emit minimizeRequested();
        break;
    case Button::Maximize:
        emit maximizeRequested();
        break;
    case Button::Close:
        emit closeRequested();
        break;
    case Button::None:
        break;
    }
}

void TitleBar::mouseMoveEvent(QMouseEvent* event) {
    setNativeHover(buttonAt(event->pos()));
    QWidget::mouseMoveEvent(event);
}

void TitleBar::leaveEvent(QEvent* event) {
    setNativeHover(Button::None);
    QWidget::leaveEvent(event);
}

void TitleBar::mousePressEvent(QMouseEvent* event) {
    const Button button = buttonAt(event->pos());
    if (event->button() == Qt::LeftButton && button != Button::None) {
        setNativePressed(button);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseReleaseEvent(QMouseEvent* event) {
    const Button pressed = pressed_;
    setNativePressed(Button::None);
    if (event->button() == Qt::LeftButton && pressed != Button::None &&
        buttonAt(event->pos()) == pressed) {
        trigger(pressed);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

bool TitleBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window_ &&
        (event->type() == QEvent::ActivationChange || event->type() == QEvent::WindowStateChange ||
         event->type() == QEvent::WindowTitleChange)) {
        update();
        mark_->update();
    }
    return QWidget::eventFilter(watched, event);
}

void TitleBar::paintButton(QPainter& painter, Button button) const {
    const theme::Tokens& t = theme::current();
    const QRect r = buttonRect(button);
    const bool active = windowActive();
    const bool hovered = hovered_ == button;
    const bool pressed = pressed_ == button;

    QColor ink = active ? t.text : t.faint;
    if (button == Button::Close && (hovered || pressed)) {
        painter.fillRect(r, pressed ? kClosePressed : kCloseHover);
        ink = QColor(Qt::white);
    } else if (pressed) {
        painter.fillRect(r, t.lineStrong);
    } else if (hovered) {
        painter.fillRect(r, t.hover);
    }

    // 10px glyphs drawn on the pixel grid of the device, like the system ones.
    painter.save();
    const double dpr = devicePixelRatioF();
    const double px = 1.0 / dpr;
    const double side = std::round(10.0 * dpr) / dpr;
    const QPointF c(r.left() + std::floor(r.width() / 2.0 * dpr) / dpr,
                    r.top() + std::floor(r.height() / 2.0 * dpr) / dpr);
    QPen pen(ink);
    pen.setWidthF(px);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const double half = side / 2.0;
    const QRectF box(c.x() - half + px / 2, c.y() - half + px / 2, side - px, side - px);
    switch (button) {
    case Button::Minimize:
        painter.drawLine(QPointF(box.left(), c.y() + px / 2), QPointF(box.right(), c.y() + px / 2));
        break;
    case Button::Maximize:
        if (window_->isMaximized()) {
            const double offset = std::round(2.0 * dpr) / dpr;
            const QRectF front = box.adjusted(0, offset, -offset, 0);
            painter.drawRect(front);
            QPainterPath back;
            back.moveTo(front.left() + offset, front.top());
            back.lineTo(front.left() + offset, box.top());
            back.lineTo(box.right(), box.top());
            back.lineTo(box.right(), front.bottom() - offset);
            back.lineTo(front.right(), front.bottom() - offset);
            painter.drawPath(back);
        } else {
            painter.drawRect(box);
        }
        break;
    case Button::Close:
        painter.setRenderHint(QPainter::Antialiasing, true);
        pen.setWidthF(std::max(1.0, dpr) / dpr);
        painter.setPen(pen);
        painter.drawLine(box.topLeft(), box.bottomRight());
        painter.drawLine(box.topRight(), box.bottomLeft());
        break;
    case Button::None:
        break;
    }
    painter.restore();
}

void TitleBar::paintEvent(QPaintEvent*) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    painter.fillRect(rect(), t.bg);
    painter.fillRect(QRect(0, height() - 1, width(), 1), t.line);

    // Title, centred on the whole bar and kept clear of menus and controls.
    const QString secondary = secondary_.isEmpty() ? QString()
                                                   : QStringLiteral("  —  ") + secondary_;
    if (!primary_.isEmpty() || !secondary.isEmpty()) {
        const bool active = windowActive();
        const QFont primaryFont = theme::font(theme::TextRole::BodyMedium, 13.0);
        const QFont secondaryFont = theme::font(theme::TextRole::Body, 13.0);
        const QFontMetrics pm(primaryFont);
        const QFontMetrics sm(secondaryFont);
        const int left = dialog_ ? mark_->geometry().right() + 12 : menuBar_->geometry().right() + kTitleGap;
        const int right = (trailing_->isVisible() && trailing_->width() > 6
                               ? trailing_->geometry().left()
                               : width() - buttonsWidth()) -
                          kTitleGap;
        const int available = right - left;
        if (available > 40) {
            QString first = primary_;
            QString second = secondary;
            int total = pm.horizontalAdvance(first) + sm.horizontalAdvance(second);
            if (total > available) {
                second.clear();
                first = pm.elidedText(primary_, Qt::ElideRight, available);
                total = pm.horizontalAdvance(first);
            }
            int x = dialog_ ? left : (width() - total) / 2;
            x = std::max(left, std::min(x, right - total));
            const QRect band(0, 0, width(), height() - 1);
            painter.setFont(primaryFont);
            painter.setPen(active ? t.text : t.faint);
            painter.drawText(QRect(x, band.top(), pm.horizontalAdvance(first) + 1, band.height()),
                             Qt::AlignVCenter | Qt::AlignLeft, first);
            if (!second.isEmpty()) {
                painter.setFont(secondaryFont);
                painter.setPen(active ? t.muted : t.faint);
                painter.drawText(QRect(x + pm.horizontalAdvance(first), band.top(),
                                       sm.horizontalAdvance(second) + 1, band.height()),
                                 Qt::AlignVCenter | Qt::AlignLeft, second);
            }
        }
    }

    if (buttonsVisible_) {
        for (Button button : {Button::Minimize, Button::Maximize, Button::Close}) {
            if (!buttonRect(button).isEmpty()) {
                paintButton(painter, button);
            }
        }
    }
}

}  // namespace rl::frame
