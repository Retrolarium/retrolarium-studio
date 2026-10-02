#pragma once

#include "smb/data/level_records.hpp"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>

namespace rl::smb {

// One area of a levels resource, as the game stores it.
struct SmbArea {
    QString name;               // "ground_1", "castle_3"
    int areaCapacity = 0;       // bytes the ROM leaves for the object stream
    int enemyCapacity = 0;      // and for the enemy stream
    // timer, entrance, foreground or colour, area style, background, terrain
    QVector<int> header = QVector<int>(6, 0);
    QVector<SmbAreaObject> objects;
    QVector<SmbEnemyRecord> enemies;
    // A stream that fills its capacity exactly and leaves its terminator to
    // the next area's first byte.
    bool areaImplicitTerminator = false;
    bool enemyImplicitTerminator = false;

    // Bytes the streams take, terminators included (2-byte header, 2 per
    // object, 3 per entrance record, 2 per other enemy record).
    int areaBytes() const;
    int enemyBytes() const;
    // The streams fit: within capacity, or one over with an implicit
    // terminator.
    bool areaFits() const;
    bool enemiesFit() const;
};

// One of the player physics tables of the world resource: bytes indexed by
// the game's state (jump speed class, swimming, climbing).
struct SmbPhysicsTable {
    QString name;               // "jump_gravity"
    bool isSigned = false;
    QVector<int> values;
};

// The shared object and enemy ranges of a pooled levels resource
// (capacity_pool): all areas pack into them one after another.
struct SmbLevelPool {
    bool pooled = false;
    int objectUsed = 0;
    int objectSize = 0;
    int enemyUsed = 0;
    int enemySize = 0;
    bool fits() const { return objectUsed <= objectSize && enemyUsed <= enemySize; }
};

// One route step of a world: the area the course plays.
struct SmbRoute {
    int areaType = 0;           // index into area types: water, ground, underground, castle
    int areaIndex = 0;
    bool alternate = false;
};

// documents/resources.json of an SMB project: the NES resource bundle the
// kit extracts from the ROM. Typed reads of the resources the studio edits;
// writes go through whole-resource replacement so unknown keys survive.
class SmbResourceDocument {
public:
    static SmbResourceDocument load(const QString& path);  // throws SmbError
    void save(const QString& path) const;                  // throws SmbError

    const QJsonObject& root() const { return root_; }
    QString game() const;       // "smb1"
    QString revision() const;   // "ju"

    // Resources by id, and the ids carrying a codec ("smb.levels.v1").
    QStringList resourceIds(const QString& codec) const;
    QJsonObject resource(const QString& id) const;
    void setResource(const QString& id, const QJsonObject& resource);

    // Levels.
    QStringList areaNames(const QString& levels) const;
    SmbArea area(const QString& levels, const QString& name) const;
    void setArea(const QString& levels, const SmbArea& area);
    QString rendering(const QString& levels) const;   // "smb1", "ann", "smb2"
    // Whether the kit packs all areas into shared ranges, and the bytes they
    // take as it packs them, with `replacement` in place of its area.
    bool levelsPooled(const QString& levels) const;
    SmbLevelPool levelPool(const QString& levels, const SmbArea* replacement = nullptr) const;
    QVector<SmbArea> areas(const QString& levels) const;
    // The shared ranges `areas` take, packed in their order.
    static SmbLevelPool poolOf(const QVector<SmbArea>& areas);
    QStringList areaTypes(const QString& levels) const;

    // World routes: worlds are 1-based.
    int worldCount(const QString& world) const;
    QVector<SmbRoute> routes(const QString& world, int number) const;
    // Replaces one step; the number of steps per world is fixed. Throws
    // SmbError for a step that does not exist or an out-of-range target.
    void setRoute(const QString& world, int number, int step, const SmbRoute& route);
    QVector<SmbPhysicsTable> physicsTables(const QString& world) const;
    void setPhysicsValue(const QString& world, int table, int index, int value);  // throws SmbError

    // Pattern tiles (2bpp, pixel values 0-3): 0-255 sprites, 256-511 background.
    int tileCount(const QString& chr) const;
    int pixel(const QString& chr, int tile, int x, int y) const;
    QVector<quint8> tilePixels(const QString& chr, int tile) const;  // 64, row-major
    void setTilePixels(const QString& chr, int tile, const QVector<quint8>& pixels);

    // Background metatiles: the four tiles (row-major: top left, top right,
    // bottom left, bottom right) and the palette of an encoded metatile.
    std::array<int, 4> metatileTiles(const QString& metatiles, int encoded) const;
    static int metatilePalette(int encoded) { return (encoded >> 6) & 3; }
    // Groups (one per background palette) and their sizes.
    QStringList metatileGroups(const QString& metatiles) const;
    QVector<int> metatileGroupSizes(const QString& metatiles) const;
    // Sets one quadrant (0-3 row-major) of an encoded metatile to a
    // background pattern 0-255. Throws SmbError.
    void setMetatileTile(const QString& metatiles, int encoded, int quadrant, int tile);

    // PPU palettes.
    QStringList packetNames(const QString& palettes) const;
    QVector<int> packetValues(const QString& palettes, const QString& name) const;
    int packetAddress(const QString& palettes, const QString& name) const;   // PPU address, $3F00 up
    // The 32 colours the PPU holds for a palette key: "ground", or a base
    // packet with the packets the game writes over it, "ground+day_snow".
    QVector<int> paletteFor(const QString& palettes, const QString& key) const;
    void setPacketValue(const QString& palettes, const QString& name, int entry, int color);
    int dayBackground(const QString& palettes) const;
    int nightBackground(const QString& palettes) const;
    QString paletteModel(const QString& palettes) const;
    QVector<int> tableValues(const QString& table) const;  // nes.palette-table.v1
    void setTableValue(const QString& table, int entry, int color);

    // Player frames: the 2x4 tiles of a frame (blank tiles included).
    int playerFrameCount(const QString& frames) const;
    QVector<int> playerFrameTiles(const QString& frames, int frame) const;
    int playerTileBase(const QString& frames) const;
    int playerBlankTile(const QString& frames) const;

private:
    int indexOf(const QString& id) const;
    void decodeTiles(const QString& chr) const;

    QJsonObject root_;
    // Decoded pattern pixels of the last tile resource read, 64 per tile.
    mutable QString tilesId_;
    mutable QVector<quint8> tiles_;
};

}  // namespace rl::smb
