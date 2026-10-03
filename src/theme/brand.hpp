#pragma once

#include "theme/tokens.hpp"

#include <QIcon>
#include <QPixmap>

namespace rl::theme::brand {

// The Retrolarium brand (resources/brand, "Scanline R"): an R of CRT
// scanlines on a 16x16 grid with a terminal cursor at its baseline.

// The window and taskbar icon: the accent tile in the light theme, the dark
// tile in the dark one. Small sizes come from the pixel-snapped PNGs.
QIcon appIcon(Mode mode);

// The mark alone for `mode`'s background, `size` logical pixels square at
// `devicePixelRatio`. Sizes that are a multiple of 16 device pixels keep
// every scanline on whole pixels. `muted` draws it for an inactive window.
QPixmap mark(Mode mode, int size, qreal devicePixelRatio, bool muted = false);

}  // namespace rl::theme::brand
