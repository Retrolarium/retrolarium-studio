#include "theme/typography.hpp"

#include <QFontDatabase>
#include <QStringList>

namespace rl::theme {

namespace {

struct Face {
    const char* resource;
    QString family;  // as registered; empty when loading failed
};

// Qt 5 on Windows registers static TTF weights under their legacy family
// names ("Barlow Medium", "IBM Plex Mono SemiBold"), so each weight is looked
// up by the family its file reported instead of by weight alone.
Face gFaces[] = {
    {":/fonts/Barlow-Regular.ttf", {}},
    {":/fonts/Barlow-Medium.ttf", {}},
    {":/fonts/Barlow-SemiBold.ttf", {}},
    {":/fonts/BarlowCondensed-Medium.ttf", {}},
    {":/fonts/BarlowCondensed-SemiBold.ttf", {}},
    {":/fonts/IBMPlexMono-Regular.ttf", {}},
    {":/fonts/IBMPlexMono-Medium.ttf", {}},
    {":/fonts/IBMPlexMono-SemiBold.ttf", {}},
    // Barlow has no Cyrillic: a Russian interface sets in IBM Plex Sans, and
    // Barlow borrows its Cyrillic letters from it everywhere else.
    {":/fonts/IBMPlexSans-Regular.ttf", {}},
    {":/fonts/IBMPlexSans-Medium.ttf", {}},
    {":/fonts/IBMPlexSans-SemiBold.ttf", {}},
    {":/fonts/IBMPlexSansCondensed-Medium.ttf", {}},
    {":/fonts/IBMPlexSansCondensed-SemiBold.ttf", {}},
};

enum FaceIndex {
    kBarlow = 0,
    kBarlowMedium,
    kBarlowSemiBold,
    kCondensedMedium,
    kCondensedSemiBold,
    kMono,
    kMonoMedium,
    kMonoSemiBold,
    kSans,
    kSansMedium,
    kSansSemiBold,
    kSansCondensedMedium,
    kSansCondensedSemiBold,
};

bool gLoaded = false;
bool gCyrillic = false;
// Plex Sans is wider than Barlow; this keeps a line close to its Latin length.
constexpr double kPlexScale = 0.94;

QString familyOr(FaceIndex index, const char* fallback) {
    const QString& family = gFaces[index].family;
    return family.isEmpty() ? QString::fromLatin1(fallback) : family;
}

}  // namespace

void loadFonts() {
    bool all = true;
    for (Face& face : gFaces) {
        const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(face.resource));
        const QStringList families =
            id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
        face.family = families.isEmpty() ? QString() : families.first();
        all = all && !face.family.isEmpty();
    }
    gLoaded = all;
    // Cyrillic text in a Barlow face falls back to the matching Plex face.
    const QPair<FaceIndex, FaceIndex> fallbacks[] = {
        {kBarlow, kSans}, {kBarlowMedium, kSansMedium}, {kBarlowSemiBold, kSansSemiBold},
        {kCondensedMedium, kSansCondensedMedium}, {kCondensedSemiBold, kSansCondensedSemiBold}};
    for (const auto& [barlow, plex] : fallbacks) {
        if (!gFaces[barlow].family.isEmpty() && !gFaces[plex].family.isEmpty()) {
            QFont::insertSubstitution(gFaces[barlow].family, gFaces[plex].family);
        }
    }
}

void setCyrillicInterface(bool cyrillic) {
    gCyrillic = cyrillic;
}

bool fontsLoaded() {
    return gLoaded;
}

QString fontReport() {
    QStringList lines;
    for (const Face& face : gFaces) {
        lines << QStringLiteral("%1 -> %2").arg(QString::fromLatin1(face.resource),
                                                face.family.isEmpty() ? QStringLiteral("<failed>")
                                                                      : face.family);
    }
    return lines.join(QLatin1Char('\n'));
}

void setLetterSpacingEm(QFont& font, double em) {
    font.setLetterSpacing(QFont::PercentageSpacing, 100.0 + em * 100.0);
}

QFont font(TextRole role, double cssPixels) {
    QFont result;
    const bool plex = gCyrillic && role != TextRole::Mono && role != TextRole::MonoMedium &&
                      role != TextRole::MonoSemiBold;
    switch (role) {
    case TextRole::Body:
        result = QFont(familyOr(plex ? kSans : kBarlow, "Segoe UI"));
        result.setWeight(QFont::Normal);
        break;
    case TextRole::BodyMedium:
        result = QFont(familyOr(plex ? kSansMedium : kBarlowMedium, "Segoe UI"));
        result.setWeight(QFont::Medium);
        break;
    case TextRole::Heading:
        result = QFont(familyOr(plex ? kSansCondensedSemiBold : kCondensedSemiBold, "Segoe UI Semibold"));
        result.setWeight(QFont::DemiBold);
        break;
    case TextRole::Caps:
        result = QFont(familyOr(plex ? kSansCondensedSemiBold : kCondensedSemiBold, "Segoe UI Semibold"));
        result.setWeight(QFont::DemiBold);
        result.setCapitalization(QFont::AllUppercase);
        setLetterSpacingEm(result, 0.08);
        break;
    case TextRole::Mono:
        result = QFont(familyOr(kMono, "Consolas"));
        result.setWeight(QFont::Normal);
        break;
    case TextRole::MonoMedium:
        result = QFont(familyOr(kMonoMedium, "Consolas"));
        result.setWeight(QFont::Medium);
        break;
    case TextRole::MonoSemiBold:
        result = QFont(familyOr(kMonoSemiBold, "Consolas"));
        result.setWeight(QFont::DemiBold);
        break;
    }
    if (role == TextRole::Mono || role == TextRole::MonoMedium || role == TextRole::MonoSemiBold) {
        result.setStyleHint(QFont::Monospace);
        result.setFixedPitch(true);
    } else {
        result.setStyleHint(QFont::SansSerif);
    }
    // The prototype is specified in CSS pixels at 96 dpi; points keep the
    // fractional sizes and scale with the display like the rest of the UI.
    result.setPointSizeF(cssPixels * 0.75 * (plex ? kPlexScale : 1.0));
    result.setHintingPreference(QFont::PreferVerticalHinting);
    return result;
}

}  // namespace rl::theme
