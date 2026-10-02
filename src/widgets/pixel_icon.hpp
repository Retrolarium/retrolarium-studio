#pragma once

#include <QImage>
#include <QPainter>
#include <QRect>

#include <algorithm>

namespace rl::ui {

// Pixel art as a small square icon: transparent edges cut off, scaled up by
// a whole factor (or down smoothly when too big) and centred, so previews in
// lists stay crisp.
inline QImage pixelIcon(const QImage& source, int box) {
    if (source.isNull() || box <= 0) {
        return {};
    }
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) != 0) {
                bounds |= QRect(x, y, 1, 1);
            }
        }
    }
    const QImage art = bounds.isEmpty() ? image : image.copy(bounds);
    const int largest = std::max(art.width(), art.height());
    QImage scaled;
    if (largest <= box) {
        const int factor = std::max(1, box / largest);
        scaled = art.scaled(art.width() * factor, art.height() * factor, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    } else {
        scaled = art.scaled(box, box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QImage result(box, box, QImage::Format_ARGB32);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.drawImage((box - scaled.width()) / 2, (box - scaled.height()) / 2, scaled);
    return result;
}

}  // namespace rl::ui
