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
    QVector<int> cram;      // 64 words the recorded palette blocks write
};

SpriteContext spriteContext(const Documents& documents, const QString& block);

// Draws `pieces` (one mapping of the mappings document) into `image` with
// the object at `origin`. Static pieces without a known context draw as
// outlines; dynamic ones use `cram` with `line` when theirs is zero.
// Earlier pieces are in front.
void drawMapping(QImage& image, QPoint origin, const QJsonArray& pieces, const Documents& documents,
                 const SpriteContext& context, const QVector<int>& cram, int line);

// A piece's rectangle relative to the object (the object's flips mirror it).
QRect pieceRect(const QJsonObject& piece, int attribute);

}  // namespace rl::alien_soldier
