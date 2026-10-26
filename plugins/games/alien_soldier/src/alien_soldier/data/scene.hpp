#pragma once

#include "alien_soldier/data/documents.hpp"

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

#include <vector>

// What a stage or a static screen looks like, built from the documents the
// way the game builds it: the asset lists' decompress-to-RAM writes make the
// maps, their VRAM loads the tiles, the palette list and the visual loader's
// command the colours. A plane reads its maps through a descriptor: the
// coarse map (32x32 chunk indices), the chunks (8x8 pattern indices) and the
// patterns (4x4 name-table words).
namespace rl::alien_soldier {

inline constexpr quint32 kRamBase = 0xFFFF0000u;

// The bytes a VRAM load writes: commands 1 and 5 copy a counted block (the
// first block of an uncompressed art payload), 2, 3 and 7 the tiles of a
// compressed payload. Empty when no document holds the source.
QByteArray vramSource(const Documents& documents, int command, const QString& label);

// The 64 CRAM words a palette offset list leaves (the common command first);
// a command played over them (a visual loader's).
QVector<int> paletteList(const QJsonObject& palettes, const QString& list);
void playPaletteCommand(QVector<int>& cram, const QJsonObject& palettes, const QString& label, int offset = 0);

struct Plane {
    bool present = false;
    quint32 coarse = 0;
    quint32 chunks = 0;
    quint32 patterns = 0;
    int yOffset = 0;  // added to Y before the map lookup
    // Where the plane is read from for a scene position (x, y):
    // ((x >> shift) + origin.x, y + origin.y), across modulo `wrap` when
    // that is set.
    QPoint origin;
    int shift = 0;
    int wrap = 0;
    // A plane the stage never streams keeps the 64 cells (512 pixels) the
    // entry drew from this X, repeated (-1: streamed).
    int fixedX = -1;
};

struct RamWrite {
    QString source;
    int offset = 0;  // into the RAM image
    int size = 0;
};

// A routine that rewrites map words in RAM after loading (see stages.py and
// screens.py): adjust-rows makes a word (word & $1FFF) + add; update-indices
// moves a non-zero tile index by `tiles`, keeps the flip bits and adds `add`.
struct RamTransform {
    int offset = 0;  // into the RAM image
    int words = 0;
    bool rows = false;  // adjust-rows; else update-indices
    int add = 0;
    int tiles = 0;
};

// The word a map must hold so that the transform turns it into `shown`;
// -1 when no word does.
int untransform(const RamTransform& transform, int shown);

struct Scene {
    QByteArray vram;    // 64 KiB
    QByteArray ram;     // the $FFFF0000 page, 64 KiB
    QVector<int> cram;  // 64 words
    Plane planes[2];
    int backdrop = 0x10;  // CRAM index of the background colour
    QVector<RamWrite> writes;
    QVector<RamTransform> transforms;
    // A credits scene's map sits in a plane's name table in VRAM, 64 cells
    // wide: its address (or -1) and rows.
    int nameTable = -1;
    int nameTableRows = 0;
    // Maps a stage loads while it runs (stages.json `later`): the scene as
    // they leave it, and the first scene X drawn from them (a screen past
    // the camera X that loads them), in order.
    std::vector<Scene> later;
    std::vector<int> laterFrom;
    // The stage's fights elsewhere in the map (stages.json `arenas`), drawn
    // with the maps, art and colours their boss loads; an inline one is shown
    // with the stage (Z-Leo's), the others apart (Back Stringer's web).
    std::vector<Scene> arenas;
    std::vector<QRect> arenaRects;
    std::vector<bool> arenaInline;
    // A floor a raster split shows (stages.json `floor`, Epsilon 1's): in
    // `floorRect` plane B reads rows of `floorPerRow` RAM blocks (4x4 name
    // words at the RAM offset `floorRam`) instead, repeated across.
    QRect floorRect;
    int floorRam = 0;
    QVector<int> floorBlocks;
    int floorPerRow = 0;

    // The scene a world X is drawn from: this one or a later load's.
    const Scene& at(int x) const;

    // The map entries a world position (plane coordinates) reads.
    int coarseOffset(int plane, int x, int y) const;  // into ram, or -1
    int chunkOffset(int plane, int x, int y) const;
    int patternOffset(int plane, int x, int y) const;
    int nameWord(int plane, int x, int y) const;
    // Whether the position reads maps the scene loaded (not past them).
    bool defined(int plane, int x, int y) const;
    // How far the loaders wrote past a base address (bytes).
    int region(quint32 base) const;
    // The part of a plane's coarse map that holds chunks, in pixels.
    QSize extent(int plane) const;
    // Which write last put a RAM byte there, or -1.
    int writerOf(int offset) const;
};

// A stage slot of stages.json, with the edited maps and art.
Scene stageScene(const Documents& documents, int stage);
// A static screen of screens.json.
Scene screenScene(const Documents& documents, int screen);

// Composite both planes (A over B, high priority first, colour 0 clear) of
// the world area `area` (plane A coordinates; plane B follows its origin).
// `high`, when given, receives a mask of the same size: opaque where a
// high-priority tile pixel is drawn (low-priority sprites go behind it).
QImage renderScene(const Scene& scene, const QRect& area, bool planeA = true, bool planeB = true,
                   QImage* high = nullptr);
// A scene's name table in VRAM, whole (64 cells by its rows).
QImage renderNameTable(const Scene& scene);
int nameTableWord(const Scene& scene, QPoint cell);

// A plane's chunk (256x256) or pattern (32x32) as it draws, colour 0 clear.
QImage chunkImage(const Scene& scene, int plane, int chunk);
QImage patternImage(const Scene& scene, int plane, int pattern);
// How many chunks and patterns the scene loaded for a plane.
int chunkCount(const Scene& scene, int plane);
int patternCount(const Scene& scene, int plane);

// One 8x8 tile of VRAM through a CRAM line, colour 0 transparent.
QImage tileImage(const QByteArray& vram, int tile, const QVector<int>& cram, int line);

// A 4bpp tile (32 bytes) as 64 pen indices.
QVector<quint8> tilePixels(const QByteArray& tile);
QRgb cramColor(int word);

}  // namespace rl::alien_soldier
