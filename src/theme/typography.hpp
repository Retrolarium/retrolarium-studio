#pragma once

#include <QFont>
#include <QString>

namespace rl::theme {

// Text roles of the design. Sizes are CSS pixels of the prototype; the helper
// converts them to fractional points so 12.5px and 11.5px survive.
enum class TextRole {
    Body,           // Barlow 400, 13px
    BodyMedium,     // Barlow 500
    Heading,        // Barlow Condensed 600
    Caps,           // Barlow Condensed 600, uppercase, +0.08em
    Mono,           // IBM Plex Mono 400
    MonoMedium,     // IBM Plex Mono 500
    MonoSemiBold,   // IBM Plex Mono 600
};

// Registers the bundled fonts. Call once after QApplication exists.
void loadFonts();

// True when every bundled family registered; false means fallbacks are used.
bool fontsLoaded();

// Names actually registered with the font database, for diagnostics.
QString fontReport();

QFont font(TextRole role, double cssPixels);

// Letter spacing in em, as written in CSS (0.08em -> 108%).
void setLetterSpacingEm(QFont& font, double em);

namespace size {
inline constexpr double kBody = 13.0;
inline constexpr double kMenu = 12.5;
inline constexpr double kRow = 12.5;
inline constexpr double kSmall = 12.0;
inline constexpr double kMeta = 11.0;
inline constexpr double kStatus = 11.5;
inline constexpr double kCaps = 11.5;
inline constexpr double kRail = 9.5;
inline constexpr double kTitle = 21.0;
inline constexpr double kProject = 18.0;
inline constexpr double kPanelTitle = 15.0;
}  // namespace size

}  // namespace rl::theme
