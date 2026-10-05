#pragma once

#include "nes/bundle.hpp"

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QVector>

namespace rl::doraemon {

// The worlds with hierarchical maps (doraemon.hierarchy.v1) and the one
// with screen streams. Each world draws from its own GNROM CHR bank.
enum class World { One = 1, Two = 2, Three = 3 };

QString worldLabel(World world);
QString hierarchyId(World world);           // "world1-hierarchy"
QString chrId(World world);                 // "chr-bank-0" ...
// Palette presets of a world: "world1-palette-00" .. (12 / 9 / 11).
QStringList paletteIds(const nes::Bundle& bundle, World world);

namespace ids {
inline const QString kRoomSelectors = QStringLiteral("world3-room-palette-selectors");
inline const QString kWorld2Metatiles = QStringLiteral("world2-metatiles");
}  // namespace ids

// Where the game takes a World 1 / World 3 map cell's colours from
// (level_area_palettes.py): the city's cross-marked north uses preset 1,
// the rest 0; the underground's rooms their own presets; a World 3 room
// the preset its selector byte names.
int areaPreset(const nes::Bundle& bundle, World world, const QString& map, int column, int row);
// A hierarchy map's big-block ids, row-major.
QVector<int> mapCellsIn(const nes::Bundle& bundle, World world, const QString& map, int* width = nullptr,
                        int* height = nullptr);

// Draws Doraemon's backgrounds from the bundle; built per bundle revision,
// with block images cached per preset.
class DoraemonPainter {
public:
    explicit DoraemonPainter(const nes::Bundle& bundle);

    QColor color(int index) const;
    // Four colours of a preset row: rows 0-3 background, 4-7 sprites.
    QVector<int> colours(World world, int preset, int row) const;
    // A background pattern (0-255) of the world's bank, or a sprite pattern
    // with `sprite`.
    QImage tile(World world, int tile, const QVector<int>& colours, bool sprite = false) const;
    // A pattern of CHR bank 3: the title and help screens draw from its
    // second table, the ending's picture from its first.
    QImage shellTile(int tile, const QVector<int>& colours, int table = 1) const;

    // A 16x16 small block and a 32x32 big block of a hierarchy world in a
    // preset's colours.
    QImage smallBlock(World world, int block, int preset) const;
    QImage bigBlock(World world, int block, int preset) const;
    // A World 2 16x16 metatile in a background preset.
    QImage metatile(int metatile, int preset) const;
    // A whole map at one pixel per pixel (big blocks of 32x32).
    QImage map(World world, const QString& map) const;

    QJsonObject hierarchy(World world) const;
    QVector<int> mapCells(World world, const QString& map, int* width = nullptr, int* height = nullptr) const;

private:
    const nes::Bundle* bundle_;
    QHash<QString, QVector<quint8>> chr_;
    QHash<int, QJsonObject> hierarchies_;
    mutable QHash<quint64, QImage> cache_;
};

}  // namespace rl::doraemon
