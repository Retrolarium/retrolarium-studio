#include "castle/render/room_painter.hpp"

#include "nes/palette.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>

namespace rl::castle {

CastlePainter::CastlePainter(const CastleLevels& levels, const nes::Bundle& graphics, const CastleArt& art)
    : width_(levels.width()), height_(levels.height()) {
    chr_ = graphics.allPixels(art.chr);
    palette_ = graphics.tableValues(art.roomPalette);
    model_ = graphics.paletteModel(art.roomPalette);
    // The cell visuals the graphics bundle edits; the room document's own
    // tables when the bundle lacks them.
    const QJsonArray visuals = art.cellVisuals.isEmpty()
                                   ? QJsonArray()
                                   : graphics.resource(art.cellVisuals).value(QStringLiteral("records")).toArray();
    if (!visuals.isEmpty()) {
        for (const QJsonValue& value : visuals) {
            const QJsonObject record = value.toObject();
            leftTiles_ << record.value(QStringLiteral("left_tile")).toInt();
            rightTiles_ << record.value(QStringLiteral("right_tile")).toInt();
            ownPalettes_ << (record.value(QStringLiteral("palette")).toInt() & 3);
        }
    } else {
        leftTiles_ = levels.renderingInts(QStringLiteral("left_tiles"));
        rightTiles_ = levels.renderingInts(QStringLiteral("right_tiles"));
        for (int value : levels.renderingInts(QStringLiteral("cell_attributes"))) {
            ownPalettes_ << (value & 3);
        }
    }
    initialAttributes_ = levels.renderingInts(QStringLiteral("initial_attributes"));
    attributeMasks_ = levels.renderingInts(QStringLiteral("attribute_masks"));
    preserving_ = levels.renderingInts(QStringLiteral("attribute_preserving_cells"));
    runtimeTypes_ = levels.renderingInts(QStringLiteral("runtime_object_types"));
    if (!art.descriptors.isEmpty()) {
        for (const QJsonValue& value : graphics.resource(art.descriptors).value(QStringLiteral("descriptors")).toArray()) {
            const QJsonObject d = value.toObject();
            descriptors_ << d.value(QStringLiteral("tile")).toInt() << d.value(QStringLiteral("attributes")).toInt();
        }
    } else {
        descriptors_ = levels.renderingInts(QStringLiteral("metasprite_descriptors"));
    }
    for (const CastleObjectPreview& preview : levels.objectPreviews()) {
        previews_.insert(preview.runtimeType, preview);
    }
}

QColor CastlePainter::color(int index) const {
    return QColor::fromRgb(nes::masterColor(index & 0x3f, model_.toStdString()));
}

QColor CastlePainter::backdrop() const {
    return color(palette_.value(0, 0x0f));
}

int CastlePainter::pixel(int tile, int x, int y) const {
    const int at = tile * 64 + y * 8 + x;
    return at >= 0 && at < chr_.size() ? chr_.at(at) : 0;
}

QVector<int> CastlePainter::cellPalettes(const QVector<int>& cells) const {
    // As the room loader writes them: the initial attribute bytes, then
    // every cell that does not keep what is there sets its quadrant. The
    // room starts at tile column 4, row 7, across two name tables.
    QVector<int> attributes(128, 0);
    for (int i = 0; i < initialAttributes_.size() && i < attributes.size(); ++i) {
        attributes[i] = initialAttributes_.at(i);
    }
    const auto place = [](int column, int row, int* offset, int* quadrant) {
        int tileX = column * 2 + 4;
        const int tileY = row + 7;
        int table = 0;
        if (tileX >= 0x20) {
            table = 64;
            tileX -= 0x20;
        }
        *offset = table + ((tileY * 2) & 0xf8) + (tileX >> 2);
        *quadrant = (tileY & 0x02) | ((tileX >> 1) & 0x01);
    };
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const int cell = cells.value(y * width_ + x);
            if (preserving_.contains(cell)) continue;
            int offset = 0;
            int quadrant = 0;
            place(x, y, &offset, &quadrant);
            const int mask = attributeMasks_.value(quadrant, 3 << (quadrant * 2));
            const int value = ownPalettes_.value(cell) * 0x55;
            attributes[offset] = (attributes.at(offset) & (mask ^ 0xff)) | (value & mask);
        }
    }
    QVector<int> result;
    result.reserve(width_ * height_);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            int offset = 0;
            int quadrant = 0;
            place(x, y, &offset, &quadrant);
            result << ((attributes.at(offset) >> (quadrant * 2)) & 3);
        }
    }
    return result;
}

QImage CastlePainter::cell(int type, int palette) const {
    const int key = (type << 2) | (palette & 3);
    const auto found = cells_.constFind(key);
    if (found != cells_.constEnd()) {
        return *found;
    }
    QImage image(kCellWidth, kCellHeight, QImage::Format_RGB32);
    const int left = 256 + leftTiles_.value(type);
    const int right = 256 + rightTiles_.value(type);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int pen = pixel(x < 8 ? left : right, x % 8, y);
            image.setPixel(x, y, color(palette_.value((palette & 3) * 4 + pen, 0x0f)).rgb());
        }
    }
    cells_.insert(key, image);
    return image;
}

QImage CastlePainter::backgroundTile(int tile, int palette) const {
    QImage image(8, 8, QImage::Format_RGB32);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int pen = pixel(256 + (tile & 0xff), x, y);
            image.setPixel(x, y, color(palette_.value((palette & 3) * 4 + pen, 0x0f)).rgb());
        }
    }
    return image;
}

QImage CastlePainter::cellIcon(int type) const {
    return cell(type, ownPalettes_.value(type));
}

QImage CastlePainter::sprite(const CastleSprite& s) const {
    QImage image(8, 16, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    // 8x16 sprites: the low bit of the tile picks the pattern table.
    const int top = (s.tile & 1) * 256 + (s.tile & 0xfe);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int sx = s.attributes & 0x40 ? 7 - x : x;
            const int sy = s.attributes & 0x80 ? 15 - y : y;
            const int pen = pixel(top + sy / 8, sx, sy % 8);
            if (pen != 0) {
                image.setPixel(x, y, color(palette_.value((4 + (s.attributes & 3)) * 4 + pen, 0x0f)).rgb());
            }
        }
    }
    return image;
}

QImage CastlePainter::player() const {
    // PLAYER_NEUTRAL_METASPRITE ($B4C2): descriptors 6 and 7.
    constexpr int kNeutral = 6;
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    for (int i = 0; i < 2; ++i) {
        const int d = (kNeutral + i) * 2;
        painter.drawImage(i * 8, 0, sprite(CastleSprite{descriptors_.value(d), descriptors_.value(d + 1), 0, 0}));
    }
    return image;
}

int CastlePainter::runtimeType(int encodedType) const {
    return runtimeTypes_.value(encodedType, -1);
}

QVector<CastleSprite> CastlePainter::sprites(const CastleObject& object) const {
    QVector<CastleSprite> result;
    const int runtime = runtimeType(object.type);
    const auto found = previews_.constFind(runtime);
    if (runtime < 0 || found == previews_.constEnd()) {
        return result;
    }
    const int baseX = object.x * kCellWidth + (runtime >= 0x90 && runtime <= 0x95 ? 4 : 0);
    const int baseY = object.y * kCellHeight;
    for (int i = 0; i < found->xOffsets.size(); ++i) {
        const int d = found->descriptorOffset + i * 2;
        result << CastleSprite{descriptors_.value(d), descriptors_.value(d + 1), baseX + found->xOffsets.at(i), baseY};
    }
    return result;
}

QRect CastlePainter::bounds(const CastleObject& object) const {
    QRect box;
    for (const CastleSprite& s : sprites(object)) {
        box |= QRect(s.x, s.y, 8, 16);
    }
    if (box.isEmpty()) {
        box = QRect(object.x * kCellWidth, object.y * kCellHeight, kCellWidth, kCellHeight);
    }
    return box;
}

void CastlePainter::drawObject(QImage& image, const CastleObject& object) const {
    QPainter painter(&image);
    const QVector<CastleSprite> list = sprites(object);
    if (list.isEmpty()) {
        // An object the game draws no picture for (a trigger): a dot.
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 220, 94));
        painter.drawEllipse(QRect(object.x * kCellWidth + 5, object.y * kCellHeight + 1, 6, 6));
        return;
    }
    for (const CastleSprite& s : list) {
        painter.drawImage(s.x, s.y, sprite(s));
    }
}

QImage CastlePainter::room(const QVector<int>& cells, const QVector<CastleObject>& objects) const {
    QImage image(width_ * kCellWidth, height_ * kCellHeight, QImage::Format_RGB32);
    const QVector<int> palettes = cellPalettes(cells);
    {
        QPainter painter(&image);
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const int index = y * width_ + x;
                painter.drawImage(x * kCellWidth, y * kCellHeight, cell(cells.value(index), palettes.value(index)));
            }
        }
    }
    for (const CastleObject& object : objects) {
        drawObject(image, object);
    }
    return image;
}

QImage CastlePainter::objectIcon(int type) const {
    const CastleObject object{type, 0, 0};
    const QRect box = bounds(object);
    QImage image(box.right() + 1, box.bottom() + 1, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    drawObject(image, object);
    return image.copy(box);
}

}  // namespace rl::castle
