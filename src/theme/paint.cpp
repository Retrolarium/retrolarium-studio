#include "theme/paint.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <cmath>

namespace rl::theme::paint {

void cornerMarks(QPainter& painter, const QRectF& target, double length, double offset, const QColor& color,
                 unsigned corners) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(color);
    pen.setWidthF(1.0);
    pen.setCapStyle(Qt::FlatCap);
    painter.setPen(pen);

    // Pixel-centred coordinates keep 1px lines crisp at any scale factor.
    const double left = std::floor(target.left() - offset) + 0.5;
    const double top = std::floor(target.top() - offset) + 0.5;
    const double right = std::ceil(target.right() + offset) - 0.5;
    const double bottom = std::ceil(target.bottom() + offset) - 0.5;
    const double arm = length - 1.0;

    if (corners & kTopLeft) {
        painter.drawLine(QPointF(left, top), QPointF(left + arm, top));
        painter.drawLine(QPointF(left, top), QPointF(left, top + arm));
    }
    if (corners & kTopRight) {
        painter.drawLine(QPointF(right, top), QPointF(right - arm, top));
        painter.drawLine(QPointF(right, top), QPointF(right, top + arm));
    }
    if (corners & kBottomLeft) {
        painter.drawLine(QPointF(left, bottom), QPointF(left + arm, bottom));
        painter.drawLine(QPointF(left, bottom), QPointF(left, bottom - arm));
    }
    if (corners & kBottomRight) {
        painter.drawLine(QPointF(right, bottom), QPointF(right - arm, bottom));
        painter.drawLine(QPointF(right, bottom), QPointF(right, bottom - arm));
    }
    painter.restore();
}

void hatch(QPainter& painter, const QRectF& area, const QColor& color, double period, double stripe) {
    painter.save();
    painter.setClipRect(area, Qt::IntersectClip);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color);
    pen.setWidthF(stripe);
    pen.setCapStyle(Qt::FlatCap);
    painter.setPen(pen);
    const double span = area.width() + area.height();
    for (double d = -area.height(); d < span; d += period) {
        painter.drawLine(QPointF(area.left() + d, area.bottom()), QPointF(area.left() + d + area.height(), area.top()));
    }
    painter.restore();
}

void checkerboard(QPainter& painter, const QRect& area, int square, const QColor& a, const QColor& b) {
    painter.save();
    painter.setClipRect(area, Qt::IntersectClip);
    painter.fillRect(area, a);
    for (int y = area.top(), row = 0; y <= area.bottom(); y += square, ++row) {
        for (int x = area.left() + ((row & 1) ? 0 : square); x <= area.right(); x += square * 2) {
            painter.fillRect(QRect(x, y, square, square), b);
        }
    }
    painter.restore();
}

void dotGrid(QPainter& painter, const QRect& area, int spacing, const QColor& color) {
    painter.save();
    painter.setClipRect(area, Qt::IntersectClip);
    const int startX = area.left() - (area.left() % spacing);
    const int startY = area.top() - (area.top() % spacing);
    for (int y = startY; y <= area.bottom(); y += spacing) {
        for (int x = startX; x <= area.right(); x += spacing) {
            painter.fillRect(QRect(x, y, 1, 1), color);
        }
    }
    painter.restore();
}

void hairlineRect(QPainter& painter, const QRectF& rect, const QColor& color, double width) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(Qt::NoBrush);
    QPen pen(color);
    pen.setWidthF(width);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    const double inset = width / 2.0;
    painter.drawRect(rect.adjusted(inset, inset, -inset, -inset));
    painter.restore();
}

}  // namespace rl::theme::paint
