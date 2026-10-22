#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>

namespace rl::theme {

// How an icon picks its color. Themed roles follow the current theme and the
// QIcon mode (hover -> Active, selected -> Selected); a fixed color does not.
enum class IconTone {
    Muted,  // toolbar and segment buttons: muted, text on hover, accent ink when on
    Rail,   // kind rail: muted, text on hover and when on
    Text,   // always text color
    Faint,
    Accent,
    OnAccent,  // glyphs on the primary button
};

// Lucide-style outline icons (24x24 viewBox, 1.5px stroke) addressed by name.
// Names: undo, redo, save, revert, build, play, stop, pause, plus, search,
// chevron-down, chevron-right, close, levels, world, tiles, metatiles, tileset,
// palettes, sprites, text, sound, playtest, chunks, blocks, collision, paint,
// select, move, place, erase, pencil, fill, pick, flip, grid, zoom-in, zoom-out,
// eye, sun, moon, settings, folder, file, warning, info, error, check.
QIcon icon(const QString& name, IconTone tone = IconTone::Muted);

// Same glyph rendered once at an explicit size and color.
QPixmap iconPixmap(const QString& name, int logicalSize, const QColor& color, double devicePixelRatio);

bool hasIcon(const QString& name);
QStringList iconNames();

// Raw path data, so plugins can register additional glyphs at startup.
void registerIcon(const QString& name, const QString& svgPathData, bool filled = false);

}  // namespace rl::theme
