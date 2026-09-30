#include "theme/tokens.hpp"

namespace rl::theme {

namespace {

QColor rgba(int r, int g, int b, double alpha) {
    return QColor(r, g, b, static_cast<int>(alpha * 255.0 + 0.5));
}

Tokens makeDark() {
    Tokens t;
    t.mode = Mode::Dark;
    t.bg = QColor(0x17, 0x19, 0x1b);
    t.chrome = QColor(0x1e, 0x21, 0x24);
    t.panel = QColor(0x23, 0x26, 0x29);
    t.field = QColor(0x14, 0x16, 0x18);
    t.text = QColor(0xe4, 0xe6, 0xe9);
    t.muted = QColor(0xa3, 0xa6, 0xab);
    t.faint = QColor(0x6d, 0x70, 0x75);
    t.line = rgba(255, 255, 255, 0.08);
    t.lineStrong = rgba(255, 255, 255, 0.17);
    t.accent = ramp::kAccent[3];
    t.accentInk = ramp::kAccent[2];
    t.accentSoft = ramp::kAccent[7];
    t.hover = rgba(148, 188, 227, 0.10);
    t.canvas = QColor(0x0e, 0x10, 0x12);
    t.onAccent = ramp::kAccent[8];
    t.mark = rgba(255, 255, 255, 0.40);
    return t;
}

Tokens makeLight() {
    Tokens t;
    t.mode = Mode::Light;
    t.bg = QColor(0xf2, 0xf2, 0xf3);
    t.chrome = QColor(0xe9, 0xe9, 0xea);
    t.panel = ramp::kNeutral[0];
    t.field = QColor(0xff, 0xff, 0xff);
    t.text = QColor(0x1d, 0x1f, 0x20);
    t.muted = ramp::kNeutral[6];
    t.faint = ramp::kNeutral[4];
    t.line = rgba(29, 31, 32, 0.13);
    t.lineStrong = rgba(29, 31, 32, 0.24);
    t.accent = QColor(0x59, 0x80, 0xa6);
    t.accentInk = ramp::kAccent[6];
    t.accentSoft = ramp::kAccent[1];
    t.hover = rgba(89, 128, 166, 0.10);
    t.canvas = ramp::kNeutral[8];
    t.onAccent = QColor(0xff, 0xff, 0xff);
    t.mark = rgba(29, 31, 32, 0.45);
    return t;
}

}  // namespace

QString modeName(Mode mode) {
    return mode == Mode::Light ? QStringLiteral("light") : QStringLiteral("dark");
}

Mode modeFromName(const QString& name, Mode fallback) {
    if (name.compare(QLatin1String("light"), Qt::CaseInsensitive) == 0) {
        return Mode::Light;
    }
    if (name.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0) {
        return Mode::Dark;
    }
    return fallback;
}

const Tokens& tokens(Mode mode) {
    static const Tokens dark = makeDark();
    static const Tokens light = makeLight();
    return mode == Mode::Light ? light : dark;
}

}  // namespace rl::theme
