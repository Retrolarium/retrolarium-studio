#include "widgets/toast.hpp"

#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>

namespace rl::ui {

namespace {
constexpr int kPaddingX = 16;
constexpr int kPaddingY = 10;
constexpr int kMargin = 24;
constexpr int kMaxWidth = 560;
constexpr int kShowMs = 4500;

QFont toastFont() {
    return theme::font(theme::TextRole::Body, 12.5);
}
}  // namespace

Toast::Toast(QWidget* parent) : QWidget(parent), timer_(new QTimer(this)) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &QWidget::hide);
    parent->installEventFilter(this);
    hide();
}

void Toast::showMessage(const QString& text) {
    if (text.trimmed().isEmpty()) {
        return;
    }
    text_ = text;
    place();
    show();
    raise();
    update();
    timer_->start(kShowMs);
}

void Toast::place() {
    const QFontMetrics metrics(toastFont());
    const int available = std::min(kMaxWidth, parentWidget()->width() - 2 * kMargin);
    const QRect textRect = metrics.boundingRect(QRect(0, 0, available - 2 * kPaddingX - 14, 1000),
                                                Qt::TextWordWrap, text_);
    const int width = textRect.width() + 2 * kPaddingX + 14;
    const int height = textRect.height() + 2 * kPaddingY;
    setGeometry((parentWidget()->width() - width) / 2, parentWidget()->height() - height - kMargin, width, height);
}

bool Toast::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) {
        place();
    }
    return QWidget::eventFilter(watched, event);
}

void Toast::mousePressEvent(QMouseEvent*) {
    hide();
}

void Toast::paintEvent(QPaintEvent*) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    painter.fillPath(path, t.panel);
    painter.setPen(QPen(t.lineStrong, 1));
    painter.drawPath(path);
    // An accent tick on the left marks it as a notice.
    painter.fillRect(QRect(kPaddingX - 6, kPaddingY, 3, height() - 2 * kPaddingY), t.accent);
    painter.setPen(t.text);
    painter.setFont(toastFont());
    painter.drawText(rect().adjusted(kPaddingX + 6, kPaddingY, -kPaddingX, -kPaddingY),
                     Qt::TextWordWrap | Qt::AlignVCenter, text_);
}

}  // namespace rl::ui
