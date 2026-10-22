#pragma once

#include <QColor>
#include <QRect>
#include <QRectF>

class QPainter;

namespace rl::theme::paint {

enum Corner : unsigned {
    kTopLeft = 1u << 0,
    kTopRight = 1u << 1,
    kBottomLeft = 1u << 2,
    kBottomRight = 1u << 3,
    kDiagonal = kTopLeft | kBottomRight,
    kAllCorners = kTopLeft | kTopRight | kBottomLeft | kBottomRight,
};

// Registration marks: L-shaped 1px brackets sitting `offset` pixels outside
// `target`, each arm `length` pixels long.
void cornerMarks(QPainter& painter, const QRectF& target, double length, double offset, const QColor& color,
                 unsigned corners = kAllCorners);

// 45-degree hatching used for placeholders and library icons.
void hatch(QPainter& painter, const QRectF& area, const QColor& color, double period, double stripe);

// Transparency checkerboard behind sprite compositions.
void checkerboard(QPainter& painter, const QRect& area, int square, const QColor& a, const QColor& b);

// Dot grid on the map canvas ground.
void dotGrid(QPainter& painter, const QRect& area, int spacing, const QColor& color);

// A crisp 1px rectangle outline inside `rect`.
void hairlineRect(QPainter& painter, const QRectF& rect, const QColor& color, double width = 1.0);

}  // namespace rl::theme::paint
