#include "widgets/panel_tabs.hpp"

#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

namespace rl::ui {

namespace {
constexpr int kHeight = 28;
constexpr int kPaddingX = 10;
constexpr int kGap = 6;
constexpr int kSpacing = 2;

QFont labelFont() { return theme::font(theme::TextRole::Caps, 12.5); }
QFont countFont() { return theme::font(theme::TextRole::Body, 11.5); }
}  // namespace

PanelTabs::PanelTabs(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFixedHeight(kHeight);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(&theme::Theme::instance(), &theme::Theme::changed, this, [this] { update(); });
}

int PanelTabs::addTab(const QString& label, const QString& count) {
    tabs_.push_back(Tab{label, count});
    updateGeometry();
    update();
    return tabs_.size() - 1;
}

void PanelTabs::setCount(int index, const QString& count) {
    if (index < 0 || index >= tabs_.size()) {
        return;
    }
    tabs_[index].count = count;
    updateGeometry();
    update();
}

void PanelTabs::setCurrentIndex(int index) {
    if (index < 0 || index >= tabs_.size() || index == current_) {
        return;
    }
    current_ = index;
    update();
    emit currentChanged(current_);
}

QVector<QRect> PanelTabs::tabRects() const {
    const QFontMetrics labelMetrics(labelFont());
    const QFontMetrics countMetrics(countFont());
    QVector<QRect> rects;
    int x = 0;
    for (const Tab& tab : tabs_) {
        int width = kPaddingX * 2 + labelMetrics.horizontalAdvance(tab.label);
        if (!tab.count.isEmpty()) {
            width += kGap + countMetrics.horizontalAdvance(tab.count);
        }
        rects.push_back(QRect(x, 0, width, kHeight));
        x += width + kSpacing;
    }
    return rects;
}

QSize PanelTabs::sizeHint() const {
    const QVector<QRect> rects = tabRects();
    return QSize(rects.isEmpty() ? 0 : rects.last().right() + 1, kHeight);
}

int PanelTabs::tabAt(const QPoint& pos) const {
    const QVector<QRect> rects = tabRects();
    for (int i = 0; i < rects.size(); ++i) {
        if (rects.at(i).contains(pos)) {
            return i;
        }
    }
    return -1;
}

void PanelTabs::mouseMoveEvent(QMouseEvent* event) {
    const int hit = tabAt(event->pos());
    if (hit != hovered_) {
        hovered_ = hit;
        setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void PanelTabs::leaveEvent(QEvent*) {
    hovered_ = -1;
    update();
}

void PanelTabs::mousePressEvent(QMouseEvent* event) {
    const int hit = tabAt(event->pos());
    if (hit < 0 || event->button() != Qt::LeftButton) {
        return;
    }
    if (hit == current_) {
        emit currentClicked(hit);
    } else {
        setCurrentIndex(hit);
    }
}

void PanelTabs::paintEvent(QPaintEvent*) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    const QVector<QRect> rects = tabRects();
    const QFont lf = labelFont();
    const QFont cf = countFont();
    const QFontMetrics labelMetrics(lf);
    for (int i = 0; i < tabs_.size(); ++i) {
        const QRect r = rects.at(i);
        const bool current = i == current_;
        painter.setFont(lf);
        painter.setPen(current || i == hovered_ ? t.text : t.muted);
        const int labelWidth = labelMetrics.horizontalAdvance(tabs_.at(i).label);
        painter.drawText(QRect(r.left() + kPaddingX, 0, r.width() - kPaddingX, kHeight), Qt::AlignVCenter,
                         tabs_.at(i).label);
        if (!tabs_.at(i).count.isEmpty()) {
            painter.setFont(cf);
            painter.setPen(t.faint);
            painter.drawText(QRect(r.left() + kPaddingX + labelWidth + kGap, 0, r.width(), kHeight), Qt::AlignVCenter,
                             tabs_.at(i).count);
        }
        if (current) {
            painter.fillRect(QRect(r.left(), kHeight - 2, r.width(), 2), t.accent);
        }
    }
}

}  // namespace rl::ui
