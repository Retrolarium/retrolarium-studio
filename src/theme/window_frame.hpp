#pragma once

#include "theme/tokens.hpp"

class QWidget;

namespace rl::theme {

// Keeps the native window frame (snapping, resizing, accessibility) but paints
// its caption in the theme colors. Windows 11 honours the caption color;
// Windows 10 only switches between the light and dark system captions.
void applyWindowFrame(QWidget* window, const Tokens& tokens);

}  // namespace rl::theme
