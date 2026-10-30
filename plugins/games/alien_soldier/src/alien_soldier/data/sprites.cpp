#include "alien_soldier/data/sprites.hpp"

#include "alien_soldier/data/scene.hpp"

#include <QJsonObject>
#include <QPainter>
#include <QSet>

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
    return spriteContextFrom(
        documents, documents[Doc::Objects].value(QStringLiteral("contexts")).toObject().value(block).toObject());
}

SpriteContext spriteContextFrom(const Documents& documents, const QJsonObject& record) {
    SpriteContext context;
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
    // Tables the code copies into a line itself (the SEGA logo's colours).
    for (const QJsonValue& value : record.value(QStringLiteral("tables")).toArray()) {
        const QJsonObject table = value.toObject();
        const int line = table.value(QStringLiteral("line")).toInt() & 3;
        const QJsonArray colours = table.value(QStringLiteral("colours")).toArray();
        for (int index = 0; index < colours.size() && index < 16; ++index)
            context.cram[line * 16 + index] = colours.at(index).toInt() & 0xEEE;
    }
    return context;
}

QRect pieceRect(const QJsonObject& piece, int attribute) {
    const int size = piece.value(QStringLiteral("size")).toInt();
    const int width = widthOf(size) * 8, height = heightOf(size) * 8;
    int x = piece.value(QStringLiteral("x")).toInt(), y = piece.value(QStringLiteral("y")).toInt();
    // The renderer mirrors a piece's offset around the object for its flips.
    if (attribute & 0x800) x = -x - width + (piece.contains(QStringLiteral("art")) ? 0 : 1);
    if (attribute & 0x1000) y = -y - height;
    return QRect(x, y, width, height);
}

QString Metatile::id() const { return QStringLiteral("meta:%1:%2:%3").arg(tile).arg(size).arg(line); }

bool Metatile::fromId(const QString& id, Metatile* metatile) {
    const QStringList parts = id.split(QLatin1Char(':'));
    if (parts.size() != 4 || parts.at(0) != QLatin1String("meta")) return false;
    bool ok[3] = {false, false, false};
    const Metatile parsed{parts.at(1).toInt(&ok[0]), parts.at(2).toInt(&ok[1]), parts.at(3).toInt(&ok[2])};
    if (!ok[0] || !ok[1] || !ok[2] || parsed.tile < 0 || parsed.tile > 0x7FF || parsed.size < 0 || parsed.size > 15 ||
        parsed.line < 0 || parsed.line > 3) {
        return false;
    }
    *metatile = parsed;
    return true;
}

bool metatileOf(const QJsonObject& piece, Metatile* metatile) {
    if (piece.contains(QStringLiteral("art"))) return false;
    const int attributes = piece.value(QStringLiteral("attributes")).toInt();
    *metatile = {attributes & 0x7FF, piece.value(QStringLiteral("size")).toInt() & 15, (attributes >> 13) & 3};
    return true;
}

QJsonObject withMetatile(const QJsonObject& piece, const Metatile& metatile) {
    QJsonObject changed = piece;
    const int attributes = piece.value(QStringLiteral("attributes")).toInt();
    changed.insert(QStringLiteral("attributes"), (attributes & ~0x67FF) | metatile.tile | metatile.line << 13);
    changed.insert(QStringLiteral("size"), (piece.value(QStringLiteral("size")).toInt() & 0xF0) | metatile.size);
    return changed;
}

namespace {

// What a block draws with, without the marks of how the kit found it.
QJsonObject drawnWith(const Documents& documents, const QString& block) {
    QJsonObject record = documents[Doc::Objects].value(QStringLiteral("contexts")).toObject().value(block).toObject();
    record.remove(QStringLiteral("borrowed"));
    record.remove(QStringLiteral("derived"));
    return record;
}

}  // namespace

QStringList blocksDrawnLike(const Documents& documents, const QString& block) {
    const QJsonObject wanted = drawnWith(documents, block);
    for (const QJsonValue& value : documents[Doc::Objects].value(QStringLiteral("groups")).toArray()) {
        const QJsonArray labels = value.toObject().value(QStringLiteral("blocks")).toArray();
        if (!labels.contains(block)) continue;
        QStringList found;
        for (const QJsonValue& label : labels) {
            if (drawnWith(documents, label.toString()) == wanted) found << label.toString();
        }
        return found;
    }
    return {block};
}

QVector<Metatile> metatilesOf(const Documents& documents, const QStringList& blocks, QVector<int>* uses) {
    QVector<Metatile> found;
    QVector<int> counts;
    const QJsonArray all = documents[Doc::Mappings].value(QStringLiteral("blocks")).toArray();
    for (const QString& label : blocks) {
        const QJsonArray mappings =
            all.at(indexOfLabel(all, label)).toObject().value(QStringLiteral("mappings")).toArray();
        for (const QJsonValue& mapping : mappings) {
            for (const QJsonValue& piece : mapping.toObject().value(QStringLiteral("pieces")).toArray()) {
                Metatile metatile;
                if (!metatileOf(piece.toObject(), &metatile)) continue;
                const int at = int(found.indexOf(metatile));
                if (at >= 0) {
                    ++counts[at];
                } else {
                    found.append(metatile);
                    counts.append(1);
                }
            }
        }
    }
    if (uses) *uses = counts;
    return found;
}

QImage metatileImage(const Metatile& metatile, const Documents& documents, const SpriteContext& context,
                     const QVector<int>& cram, int line, int side) {
    QImage image(side, side, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const QJsonObject piece{{QStringLiteral("attributes"), 0x8000 | metatile.tile | metatile.line << 13},
                            {QStringLiteral("size"), metatile.size},
                            {QStringLiteral("x"), 0},
                            {QStringLiteral("y"), 0}};
    // Unflipped, whatever the object's flips.
    SpriteContext upright = context;
    upright.attribute &= ~0x1800;
    const QPoint origin((side - widthOf(metatile.size) * 8) / 2, (side - heightOf(metatile.size) * 8) / 2);
    drawMapping(image, origin, QJsonArray{piece}, documents, upright, cram, line);
    return image;
}

QString PieceArt::id() const { return QStringLiteral("art:") + art; }

bool PieceArt::labelFromId(const QString& id, QString* art) {
    if (!id.startsWith(QLatin1String("art:")) || id.size() <= 4) return false;
    *art = id.mid(4);
    return true;
}

bool pieceArtOf(const QJsonObject& piece, PieceArt* art) {
    if (!piece.contains(QStringLiteral("art"))) return false;
    *art = {piece.value(QStringLiteral("art")).toString(), piece.value(QStringLiteral("size")).toInt() & 15,
            (piece.value(QStringLiteral("attributes")).toInt() >> 13) & 3};
    return true;
}

QStringList groupBlocks(const Documents& documents, const QString& block) {
    for (const QJsonValue& value : documents[Doc::Objects].value(QStringLiteral("groups")).toArray()) {
        const QJsonArray labels = value.toObject().value(QStringLiteral("blocks")).toArray();
        if (!labels.contains(block)) continue;
        QStringList found;
        for (const QJsonValue& label : labels) found << label.toString();
        return found;
    }
    return {block};
}

QVector<PieceArt> pieceArtsOf(const Documents& documents, const QStringList& blocks) {
    QVector<PieceArt> found;
    QSet<QString> seen;
    const QJsonArray all = documents[Doc::Mappings].value(QStringLiteral("blocks")).toArray();
    for (const QString& label : blocks) {
        const QJsonArray mappings =
            all.at(indexOfLabel(all, label)).toObject().value(QStringLiteral("mappings")).toArray();
        for (const QJsonValue& mapping : mappings) {
            for (const QJsonValue& piece : mapping.toObject().value(QStringLiteral("pieces")).toArray()) {
                PieceArt art;
                if (!pieceArtOf(piece.toObject(), &art) || seen.contains(art.art)) continue;
                seen.insert(art.art);
                found.append(art);
            }
        }
    }
    return found;
}

QImage pieceArtImage(const PieceArt& art, const Documents& documents, const SpriteContext& context,
                     const QVector<int>& cram, int line, int side) {
    QImage image(side, side, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const QJsonObject piece{{QStringLiteral("attributes"), 0x8000 | art.line << 13},
                            {QStringLiteral("size"), art.size},
                            {QStringLiteral("x"), 0},
                            {QStringLiteral("y"), 0},
                            {QStringLiteral("art"), art.art}};
    SpriteContext upright = context;
    upright.attribute &= ~0x1800;
    const QPoint origin((side - widthOf(art.size) * 8) / 2, (side - heightOf(art.size) * 8) / 2);
    drawMapping(image, origin, QJsonArray{piece}, documents, upright, cram, line);
    return image;
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
