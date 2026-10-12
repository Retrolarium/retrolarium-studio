#pragma once

#include "smb/data/resource_document.hpp"
#include "smb/render/level_renderer.hpp"

#include <QHash>
#include <QImage>
#include <QPoint>
#include <QRect>

#include <string>

namespace rl::smb {

// Where the art comes from: resource ids in the document.
struct SmbArt {
    QString chr;            // nes.chr-2bpp.v1
    QString metatiles;      // nes.metatile-2x2.v1
    QString palettes;       // nes.ppu-palette-packets.v1
    QString playerFrames;   // smb.player-frames.v1
    QString playerPalette;  // nes.palette-table.v1
};

// Draws a rendered level scene as the game would show it: background
// metatiles in the area's palette, enemies with their sprites, and the player.
// Coordinates are level pixels, 16 per metatile, rows 0-12 of the play
// field (the status bar rows are not part of the area).
class SmbLevelPainter {
public:
    SmbLevelPainter(const SmbResourceDocument& document, SmbArt art);

    // The area type ("ground") picks the palette packet; night swaps the
    // backdrop colour.
    QImage render(const SmbLevelScene& scene, const QString& areaType, bool night) const;

    QImage metatile(int value, const QString& areaType, bool night) const;
    QImage enemy(int identifier, const QString& areaType) const;
    QRect enemyBounds(const SmbPositionedEnemy& enemy, const QString& areaType) const;
    QImage player() const;
    // A background pattern (0-255) in background palette 0-3 of a packet
    // ("ground"); pixel 0 shows the backdrop colour (an NES colour index).
    QImage backgroundTile(int tile, const QString& packet, int palette, int backdrop) const;
    // An NES colour index in the project's palette model.
    QRgb nesColor(int index) const { return color(index); }
    // The player standing with its feet on the given 16px cell (column, row).
    QRect playerBounds(QPoint cell) const;

    void clearCache();

    static constexpr int kRows = 13;
    static constexpr int kCell = 16;

private:
    QRgb color(int index) const;

    const SmbResourceDocument& document_;
    SmbArt art_;
    mutable QHash<QString, QImage> cache_;
    mutable std::string model_;     // palette model, read once per cache
};

// A table of the project's level tables; empty in a project made before
// them.
QVector<int> smbLevelTable(const SmbResourceDocument& document, const QString& name);
// The sky is dark for these areas: by area type, or the header's background
// colour (foreground_or_color 4-7), against the game's background colours.
bool automaticNight(const QString& areaType, const QVector<int>& header, const QVector<int>& colors);
// The cell the player enters an area at, from its entrance control and the game's
// starting heights.
QPoint entranceCell(const QVector<int>& header, const QVector<int>& startY);
// "ground_3" -> "ground"
QString areaTypeOf(const QString& areaName);

}  // namespace rl::smb
