#pragma once

#include "alien_soldier/data/documents.hpp"

#include <QByteArray>
#include <QImage>
#include <QJsonArray>
#include <QPoint>
#include <QString>
#include <QVector>

// Drawing a sprite mapping as the game does. A static piece's tile word is
// the piece's attribute word with the object's flags exclusive-ored in and
// the object's base tile added; the tiles come from VRAM the stage, a boss
// or the object filled, which the game repository records per mapping block
// (objects.json, from config/object_art.json). A dynamic piece carries its
// own art.
namespace rl::alien_soldier {

struct SpriteContext {
    bool known = false;
    bool borrowed = false;  // taken from another block of the same file
    int attribute = 0;      // the object's: priority, palette line, flips, base tile
    QByteArray vram;        // 64 KiB with the recorded art loaded
    QVector<int> cram;      // 64 words the recorded palette blocks and colour tables write
};

SpriteContext spriteContext(const Documents& documents, const QString& block);
// The same from a record of that shape (attribute, loads, palettes).
SpriteContext spriteContextFrom(const Documents& documents, const QJsonObject& record);

// Draws `pieces` (one mapping of the mappings document) into `image` with
// the object at `origin`. Static pieces without a known context draw as
// outlines; dynamic ones use `cram` with `line` when theirs is zero.
// Earlier pieces are in front.
void drawMapping(QImage& image, QPoint origin, const QJsonArray& pieces, const Documents& documents,
                 const SpriteContext& context, const QVector<int>& cram, int line);

// A piece's rectangle relative to the object (the object's flips mirror it;
// a static piece mirrored lands a pixel right of a true mirror, as
// Sprite_RenderMapping places it).
QRect pieceRect(const QJsonObject& piece, int attribute);

// An object's metatile: what a static piece draws, its first tile (added to
// the object's base tile), its size (the size byte's low nibble,
// (width - 1) << 2 | height - 1) and its palette line (exclusive-ored with
// the object's). A piece adds a position and flips.
struct Metatile {
    int tile = 0;
    int size = 0;
    int line = 0;

    QString id() const;
    // An id() back to the metatile; false for anything else.
    static bool fromId(const QString& id, Metatile* metatile);
    bool operator==(const Metatile& other) const {
        return tile == other.tile && size == other.size && line == other.line;
    }
};

// A static piece's metatile; false for a dynamic piece, which carries its own
// art.
bool metatileOf(const QJsonObject& piece, Metatile* metatile);
// The piece drawing `metatile` instead, its position, flips, end mark and
// the size byte's high nibble kept.
QJsonObject withMetatile(const QJsonObject& piece, const Metatile& metatile);

// The blocks of `block`'s file (objects.json groups) that draw with what it
// draws with (art, attribute, colours), in file order: their pieces share
// metatiles. A block in no group stands alone.
QStringList blocksDrawnLike(const Documents& documents, const QString& block);
// The metatiles the blocks' static pieces use, in order of first use, and
// with `uses` how many pieces use each.
QVector<Metatile> metatilesOf(const Documents& documents, const QStringList& blocks, QVector<int>* uses = nullptr);
// `metatile` alone and unflipped, centred in a side x side image.
QImage metatileImage(const Metatile& metatile, const Documents& documents, const SpriteContext& context,
                     const QVector<int>& cram, int line, int side);

// A dynamic piece's art: the payload it uploads, its size (the size byte's
// low nibble) and its palette line, as a piece of the object's file draws it.
struct PieceArt {
    QString art;
    int size = 0;
    int line = 0;

    QString id() const;
    // An id() back to the art's label; false for anything else.
    static bool labelFromId(const QString& id, QString* art);
};

// A dynamic piece's art; false for a static piece.
bool pieceArtOf(const QJsonObject& piece, PieceArt* art);
// The blocks of `block`'s objects group, in file order (`block` alone when
// it is in none).
QStringList groupBlocks(const Documents& documents, const QString& block);
// The art the blocks' dynamic pieces upload, in order of first use.
QVector<PieceArt> pieceArtsOf(const Documents& documents, const QStringList& blocks);
// `art` alone and unflipped, centred in a side x side image.
QImage pieceArtImage(const PieceArt& art, const Documents& documents, const SpriteContext& context,
                     const QVector<int>& cram, int line, int side);

}  // namespace rl::alien_soldier
