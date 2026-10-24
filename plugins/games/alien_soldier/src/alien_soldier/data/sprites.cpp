#include "alien_soldier/data/sprites.hpp"

#include "alien_soldier/data/scene.hpp"

#include <QJsonObject>
#include <QPainter>

#include <algorithm>

namespace rl::alien_soldier {

namespace {

constexpr int kVramSize = 0x10000;

int widthOf(int size) { return ((size >> 2) & 3) + 1; }
int heightOf(int size) { return (size & 3) + 1; }

// The tiles of an uncompressed art payload's first block (a dynamic piece's art).
QByteArray artBytes(const Documents& documents, const QString& label) { return vramSource(documents, 1, label); }

// Draws a w x h tile sprite whose tiles run down each column before moving
// right, from `tiles` (32 bytes each).
void drawTiles(QImage& image, const QRect& rect, const QByteArray& tiles, int first, bool hflip, bool vflip,
               const QVector<int>& cram, int line) {
    const int w = rect.width() / 8, h = rect.height() / 8;
    for (int column = 0; column < w; ++column) {
        for (int row = 0; row < h; ++row) {
            const int at = (first + column * h + row) * 32;
            if (at < 0 || at + 32 > tiles.size()) continue;
            const QVector<quint8> pixels = tilePixels(tiles.mid(at, 32));
            for (int py = 0; py < 8; ++py) {
                for (int px = 0; px < 8; ++px) {
                    const int value = pixels[(vflip ? 7 - py : py) * 8 + (hflip ? 7 - px : px)];
                    if (!value) continue;
                    const int x = rect.x() + (hflip ? w - 1 - column : column) * 8 + px;
                    const int y = rect.y() + (vflip ? h - 1 - row : row) * 8 + py;
                    if (x >= 0 && y >= 0 && x < image.width() && y < image.height())
                        image.setPixel(x, y, cramColor(cram.value(line * 16 + value)));
                }
            }
        }
    }
}

}  // namespace

SpriteContext spriteContext(const Documents& documents, const QString& block) {
    SpriteContext context;
    const QJsonObject record =
        documents[Doc::Objects].value(QStringLiteral("contexts")).toObject().value(block).toObject();
    if (record.isEmpty()) return context;
    context.known = true;
    context.borrowed = record.value(QStringLiteral("borrowed")).toBool();
    context.attribute = record.value(QStringLiteral("attribute")).toInt();
    context.vram = QByteArray(kVramSize, '\0');
    for (const QJsonValue& value : record.value(QStringLiteral("loads")).toArray()) {
        const QJsonObject load = value.toObject();
        const QByteArray bytes = vramSource(documents, load.value(QStringLiteral("command")).toInt(),
                                            load.value(QStringLiteral("source")).toString());
        const int at = load.value(QStringLiteral("vram")).toInt() & 0xFFFF;
        const int length = std::min<int>(bytes.size(), kVramSize - at);
        std::copy(bytes.cbegin(), bytes.cbegin() + length, context.vram.begin() + at);
    }
    context.cram = QVector<int>(64, 0);
    const QJsonObject palettes = documents[Doc::Palettes];
    for (const QJsonValue& value : record.value(QStringLiteral("palettes")).toArray()) {
        const QJsonArray pair = value.toArray();
        playPaletteCommand(context.cram, palettes, pair.at(0).toString(), pair.at(1).toInt());
    }
    return context;
}

QRect pieceRect(const QJsonObject& piece, int attribute) {
    const int size = piece.value(QStringLiteral("size")).toInt();
    const int width = widthOf(size) * 8, height = heightOf(size) * 8;
    int x = piece.value(QStringLiteral("x")).toInt(), y = piece.value(QStringLiteral("y")).toInt();
    // The renderer mirrors a piece's offset around the object for its flips.
    if (attribute & 0x800) x = -x - width;
    if (attribute & 0x1000) y = -y - height;
    return QRect(x, y, width, height);
}

void drawMapping(QImage& image, QPoint origin, const QJsonArray& pieces, const Documents& documents,
                 const SpriteContext& context, const QVector<int>& cram, int line) {
    QPainter painter(&image);
    const int objectAttribute = context.known ? context.attribute : 0;
    for (int index = pieces.size() - 1; index >= 0; --index) {
        const QJsonObject piece = pieces.at(index).toObject();
        const QRect rect = pieceRect(piece, objectAttribute).translated(origin);
        const int attributes = piece.value(QStringLiteral("attributes")).toInt();
        const QString art = piece.value(QStringLiteral("art")).toString();
        if (!art.isEmpty()) {
            // The piece's flags with the object's exclusive-ored in, as for
            // a static piece; without a record, the view's colours.
            const int word = (attributes & 0x7FFF) ^ (objectAttribute & 0xF800);
            const int pieceLine = (word >> 13) & 3;
            drawTiles(image, rect, artBytes(documents, art), 0, word & 0x800, word & 0x1000,
                      context.known ? context.cram : cram, context.known || pieceLine ? pieceLine : line);
            continue;
        }
        if (!context.known) {
            painter.fillRect(rect, QColor(120, 140, 180, 70));
            painter.setPen(QColor(180, 200, 255, 200));
            painter.drawRect(rect.adjusted(0, 0, -1, -1));
            continue;
        }
        const int word = (((attributes & 0x7FFF) ^ (objectAttribute & 0xF800)) + (objectAttribute & 0x7FF)) & 0xFFFF;
        drawTiles(image, rect, context.vram, word & 0x7FF, word & 0x800, word & 0x1000, context.cram, (word >> 13) & 3);
    }
}

}  // namespace rl::alien_soldier
