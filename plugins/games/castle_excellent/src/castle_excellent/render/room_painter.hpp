#pragma once

#include "castle/data/levels.hpp"
#include "nes/bundle.hpp"

#include <QHash>
#include <QImage>
#include <QRect>

namespace rl::castle {

// The resources a room is drawn with, from the graphics bundle.
struct CastleArt {
    QString chr;            // nes.chr-2bpp.v1 bank the rooms use
    QString roomPalette;    // nes.palette-table.v1, 32 entries
    QString cellVisuals;    // castle.room-cell-visuals.v1
    QString descriptors;    // castle.metasprite-descriptors.v1
};

// One 8x16 sprite of an object.
struct CastleSprite {
    int tile = 0;
    int attributes = 0;
    int x = 0;
    int y = 0;
};

// Draws Castle rooms as the game does: each logical cell is two background
// tiles (the second pattern table), coloured by the attribute bytes the
// room loader writes; static objects are 8x16 sprites of their metasprite.
// Built once per document revision.
class CastlePainter {
public:
    static constexpr int kCellWidth = 16;
    static constexpr int kCellHeight = 8;

    CastlePainter(const CastleLevels& levels, const nes::Bundle& graphics, const CastleArt& art);

    int cellTypeCount() const { return leftTiles_.size(); }
    // The 2-bit palette of every cell of a room, as the loader leaves them.
    QVector<int> cellPalettes(const QVector<int>& cells) const;
    QImage cell(int type, int palette) const;               // 16x8
    // One background pattern (second pattern table) in a room palette, 8x8.
    QImage backgroundTile(int tile, int palette) const;
    // A cell type in its own palette, for the sheet.
    QImage cellIcon(int type) const;
    QImage room(const QVector<int>& cells, const QVector<CastleObject>& objects) const;   // 320x160

    QVector<CastleSprite> sprites(const CastleObject& object) const;
    // One 8x16 sprite (tile pair and attributes), transparent around.
    QImage sprite(const CastleSprite& sprite) const;
    // The player's standing frame (16x16), as the game draws it at the
    // start of a room.
    QImage player() const;
    // Pixels an object covers; a dot-sized box when it has no picture.
    QRect bounds(const CastleObject& object) const;
    QImage objectIcon(int type) const;      // cropped to its sprites, transparent around
    void drawObject(QImage& image, const CastleObject& object) const;
    int runtimeType(int encodedType) const;
    QColor backdrop() const;
    QColor color(int index) const;

private:
    int pixel(int tile, int x, int y) const;

    QVector<quint8> chr_;
    QVector<int> palette_;
    QVector<int> leftTiles_;
    QVector<int> rightTiles_;
    QVector<int> ownPalettes_;
    QVector<int> initialAttributes_;
    QVector<int> attributeMasks_;
    QVector<int> preserving_;
    QVector<int> runtimeTypes_;
    QVector<int> descriptors_;      // tile, attributes, tile, attributes...
    QHash<int, CastleObjectPreview> previews_;  // by runtime type
    int width_ = 20;
    int height_ = 20;
    QString model_;
    mutable QHash<int, QImage> cells_;          // type << 2 | palette
};

}  // namespace rl::castle
