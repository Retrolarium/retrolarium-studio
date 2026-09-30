#pragma once

#include "theme/tokens.hpp"

#include <QString>

namespace rl::theme {

class Theme;

// Builds the application style sheet from the semantic tokens.
//
// Widgets opt into roles through dynamic properties instead of object names,
// so any widget can take a role and the sheet stays the single source of
// chrome colors:
//   surface = bg | chrome | panel | canvas     background of a QFrame
//   divider = line | strong                    1px separator QFrame
//   tone    = text | muted | faint | accent    QLabel text color
//   variant = icon | rail | tool | flat        QToolButton / QPushButton look
//   field   = flat                             borderless QLineEdit inside a composite
// Fonts are never set here: letter spacing and capitalization are not
// expressible in Qt style sheets, so typography stays in code.
QString buildStyleSheet(const Tokens& tokens, const Theme& theme);

// "rgba(r, g, b, a)" for style sheet values.
QString css(const QColor& color);

}  // namespace rl::theme
