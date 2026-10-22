#pragma once

#include <QColor>
#include <QString>

namespace rl::theme {

enum class Mode { Dark, Light };

QString modeName(Mode mode);
Mode modeFromName(const QString& name, Mode fallback = Mode::Dark);

// Semantic colors of the Industry design system. Every widget reads colors from
// here; nothing in the application hardcodes a chrome color.
struct Tokens {
    Mode mode = Mode::Dark;

    QColor bg;      // title/menu bar, editor ground
    QColor chrome;  // toolbar, rail, status bar, table header
    QColor panel;   // document list, inspector, bottom panel
    QColor field;   // input background
    QColor text;
    QColor muted;
    QColor faint;
    QColor line;        // 1px hairline
    QColor lineStrong;  // field and button borders
    QColor accent;
    QColor accentInk;   // selected text, links
    QColor accentSoft;  // selected fill
    QColor hover;
    QColor canvas;    // ground behind pixel canvases
    QColor onAccent;  // text on accent fill
    QColor mark;      // corner registration marks
};

const Tokens& tokens(Mode mode);

// Colors drawn over game pixels. They do not follow the chrome theme because
// the ground under them is always the dark pixel canvas.
namespace canvas {
inline const QColor kSelection{0x94, 0xbc, 0xe3};
inline const QColor kTagFill{0x94, 0xbc, 0xe3};
inline const QColor kTagText{0x1d, 0x2d, 0x3d};
inline const QColor kCheckerA{0x1a, 0x1c, 0x1f};
inline const QColor kCheckerB{0x14, 0x16, 0x18};
inline const QColor kMark{255, 255, 255, 128};
inline const QColor kGridDot{255, 255, 255, 18};
inline const QColor kObjectOutline{255, 255, 255, 140};
inline const QColor kPageLabel{255, 255, 255, 140};
inline const QColor kSelectionFill{0x94, 0xbc, 0xe3, 46};
}  // namespace canvas

// Raw ramps of the design system, for the rare place that needs a step
// outside the semantic set (charts, gallery).
namespace ramp {
inline const QColor kAccent[9] = {QColor(0xee, 0xf6, 0xff), QColor(0xd6, 0xeb, 0xff), QColor(0xb5, 0xd9, 0xfd),
                                  QColor(0x94, 0xbc, 0xe3), QColor(0x74, 0x9d, 0xc4), QColor(0x59, 0x7e, 0xa3),
                                  QColor(0x41, 0x61, 0x80), QColor(0x2c, 0x45, 0x5d), QColor(0x1d, 0x2d, 0x3d)};
inline const QColor kNeutral[9] = {QColor(0xf5, 0xf5, 0xf8), QColor(0xe7, 0xe7, 0xea), QColor(0xd4, 0xd4, 0xd7),
                                   QColor(0xb7, 0xb7, 0xba), QColor(0x98, 0x98, 0x9b), QColor(0x7a, 0x7a, 0x7d),
                                   QColor(0x5d, 0x5d, 0x60), QColor(0x42, 0x42, 0x44), QColor(0x2b, 0x2b, 0x2d)};
}  // namespace ramp

}  // namespace rl::theme
