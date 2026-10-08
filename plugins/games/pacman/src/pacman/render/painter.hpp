#pragma once

#include "nes/bundle.hpp"

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QVector>

namespace rl::pacman {

// Ids of the resources in a Pac-Man bundle (documents/resources.json).
namespace ids {
inline const QString kMaze = QStringLiteral("maze");
inline const QString kChr = QStringLiteral("character-graphics");
inline const QString kActors = QStringLiteral("actor-frames");
inline const QString kScreens = QStringLiteral("screen-content");
inline const QString kSound = QStringLiteral("sound-streams");
inline const QString kGameplay = QStringLiteral("round-gameplay");
inline const QString kTitle = QStringLiteral("title-background");
}  // namespace ids

// The maze's fixed size and landmarks (the engine's own constants).
namespace maze {
constexpr int kRows = 27;
constexpr int kColumns = 22;
constexpr int kRleBytes = 416;
constexpr int kTunnelRow = 13;
constexpr int kDoorRow = 11, kDoorColumn = 11;
constexpr int kPacmanRow = 20, kPacmanColumn = 11;
// The power pellets: the blinking table draws these four cells and the
// eaten-slot logic finds them by row and column, so they stay put.
constexpr int kPowerPellets[4][2] = {{3, 2}, {3, 20}, {20, 2}, {20, 20}};
bool isPowerCell(int row, int column);
// The visible power pellet and its blink frame, eaten alike.
inline bool isPowerTile(int tile) { return tile == 0x01 || tile == 0x02; }
// The round counter is a byte: the build writes the maze's count into it.
constexpr int kMostPellets = 255;
// Where the game draws the maze on the screen, in tiles.
constexpr int kScreenRow = 2, kScreenColumn = 1;
QString tileName(int tile);
// The engine's checks, as the kit's codec runs them (errors first).
struct Report {
    QStringList errors;
    QStringList warnings;
    int pellets = 0;
    int powerPellets = 0;
    int rleBytes = 0;
};
Report inspect(const QVector<int>& grid);
}  // namespace maze

// Draws Pac-Man's graphics from the bundle: patterns, the maze, actor
// frames and the title screen. Built per bundle revision.
class PacmanPainter {
public:
    explicit PacmanPainter(const nes::Bundle& bundle);

    QColor color(int index) const;
    // A pattern (0-255 background, 256-511 sprites) in four NES colours;
    // pen 0 is transparent when `transparent`.
    QImage tile(int tile, const QVector<int>& colours, bool transparent = false) const;
    // Four colours of a palette table row: `palette` 0-3 background, 4-7 sprites.
    QVector<int> colours(const QString& table, int palette) const;

    // The sprite palettes of a table: entries 16-31 of a full table, all of
    // a 16-entry sprite table (intermission).
    QVector<QVector<int>> spritePalettes(const QString& table) const;

    QImage maze(const QVector<int>& grid) const;                // 176x216
    // A 16x16 actor frame: `alternate` picks the second table; sprite
    // palette `palette` ORed with each quarter's attribute bits picks from
    // `sprites` (the gameplay table's when empty).
    QImage frame(const QJsonObject& actors, bool alternate, int index, int palette,
                 const QVector<QVector<int>>& sprites = {}) const;
    // A 32x30 name table with its attribute bytes (1024 entries) in the
    // background palettes of `table`.
    QImage nameTable(const QVector<int>& cells, const QString& table) const;   // 256x240

private:
    QVector<quint8> chr_;
    const nes::Bundle* bundle_;
    QString model_;
};

}  // namespace rl::pacman
