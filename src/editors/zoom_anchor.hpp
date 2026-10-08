#pragma once

#include <QPoint>
#include <QRect>
#include <QScrollArea>
#include <QScrollBar>
#include <QWidget>

#include <algorithm>

namespace rl::editors {

// Changes a canvas's zoom so the picture under `anchor` (canvas coordinates:
// the mouse for Ctrl+wheel, the viewport's centre otherwise) stays where it
// is in the scroll area. `drawn()` is the picture's rectangle in the canvas,
// `apply()` sets the new zoom. The canvas is resized at once instead of on
// the scroll area's next layout pass, so the scroll bars can be set now.
template <typename Drawn, typename Apply>
void zoomKeepingPoint(QScrollArea* scroll, QWidget* canvas, QPoint anchor, Drawn drawn, Apply apply) {
    const QRect before = drawn();
    const auto fraction = [](int value, int start, int length) {
        return length > 0 ? std::clamp(double(value - start) / length, 0.0, 1.0) : 0.5;
    };
    const double fx = fraction(anchor.x(), before.left(), before.width());
    const double fy = fraction(anchor.y(), before.top(), before.height());
    const QPoint inViewport = canvas->mapTo(scroll->viewport(), anchor);
    apply();
    canvas->resize(scroll->viewport()->size().expandedTo(canvas->minimumSize()));
    const QRect after = drawn();
    const QPoint target(after.left() + int(fx * after.width()), after.top() + int(fy * after.height()));
    scroll->horizontalScrollBar()->setValue(target.x() - inViewport.x());
    scroll->verticalScrollBar()->setValue(target.y() - inViewport.y());
}

// The canvas point at the middle of what the scroll area shows.
inline QPoint viewportCentre(QScrollArea* scroll, QWidget* canvas) {
    return canvas->mapFrom(scroll->viewport(), scroll->viewport()->rect().center());
}

}  // namespace rl::editors
