#include "theme/brand.hpp"

#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>

namespace rl::theme::brand {

QIcon appIcon(Mode mode) {
    QIcon icon;
    if (mode == Mode::Dark) {
        // The dark tile comes in fewer sizes; the icon scales between them.
        for (int s : {16, 32, 48, 256}) {
            icon.addFile(QStringLiteral(":/brand/icon-dark-%1.png").arg(s), QSize(s, s));
        }
    } else {
        for (int s : {16, 24, 32, 48, 64, 128, 256}) {
            icon.addFile(QStringLiteral(":/brand/icon-%1.png").arg(s), QSize(s, s));
        }
    }
    return icon;
}

QPixmap mark(Mode mode, int size, qreal devicePixelRatio, bool muted) {
    // Whole device pixels per grid unit when the size allows it.
    const int device = std::max(16, int(std::lround(size * devicePixelRatio)));
    QPixmap pixmap(device, device);
    pixmap.fill(Qt::transparent);
    QSvgRenderer renderer(mode == Mode::Dark ? QStringLiteral(":/brand/mark-on-dark.svg")
                                             : QStringLiteral(":/brand/mark.svg"));
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, device % 16 != 0);
        if (muted) {
            painter.setOpacity(0.45);
        }
        renderer.render(&painter, QRectF(0, 0, device, device));
    }
    pixmap.setDevicePixelRatio(devicePixelRatio);
    return pixmap;
}

}  // namespace rl::theme::brand
