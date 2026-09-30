#include "theme/icons.hpp"

#include "theme/theme.hpp"

#include <QGuiApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>

namespace rl::theme {

namespace {

struct Glyph {
    QString path;
    bool filled = false;
};

QHash<QString, Glyph>& glyphs() {
    static QHash<QString, Glyph> table = [] {
        QHash<QString, Glyph> t;
        auto add = [&t](const char* name, const char* path, bool filled = false) {
            t.insert(QString::fromLatin1(name), Glyph{QString::fromLatin1(path), filled});
        };
        // Global actions.
        add("undo", "M9 14 4 9l5-5 M4 9h10.5a5.5 5.5 0 0 1 5.5 5.5a5.5 5.5 0 0 1-5.5 5.5H11");
        add("redo", "m15 14 5-5-5-5 M20 9H9.5A5.5 5.5 0 0 0 4 14.5A5.5 5.5 0 0 0 9.5 20H13");
        add("save",
            "M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5"
            "a2 2 0 0 1 2-2z M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7 M7 3v4a1 1 0 0 0 1 1h7");
        add("revert", "M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8 M3 3v5h5");
        add("build",
            "m15 12-8.373 8.373a1 1 0 1 1-3-3L12 9 M18 15l4-4 M21.5 11.5l-1.914-1.914A2 2 0 0 1 19"
            " 8.172V7l-2.26-2.26a6 6 0 0 0-4.202-1.756L9 2.96l.92.82A6.18 6.18 0 0 1 12 8.4V10l2 2"
            "h1.172a2 2 0 0 1 1.414.586L18.5 14.5");
        add("play", "M6 3l14 9-14 9z", true);
        add("stop", "M6 6h12v12H6z", true);
        add("pause", "M6 4h4v16H6z M14 4h4v16h-4z", true);
        add("plus", "M5 12h14 M12 5v14");
        add("search", "M3 11a8 8 0 1 0 16 0a8 8 0 1 0 -16 0 M21 21l-4.3-4.3");
        add("chevron-down", "m6 9 6 6 6-6");
        add("chevron-right", "m9 18 6-6-6-6");
        add("close", "M18 6 6 18 M6 6l12 12");
        add("check", "M20 6 9 17l-5-5");
        // Data kinds.
        add("levels", "M3 3h18v18H3z M3 9h18 M9 21V9");
        add("world",
            "M6 3v12 M18 3a3 3 0 1 0 0 6a3 3 0 0 0 0-6z M6 15a3 3 0 1 0 0 6a3 3 0 0 0 0-6z"
            " M18 9a9 9 0 0 1-9 9");
        add("tiles", "M3 3h18v18H3z M3 12h18 M12 3v18");
        add("metatiles", "M12 2 2 7l10 5 10-5-10-5z M2 17l10 5 10-5 M2 12l10 5 10-5");
        add("tileset", "M12 2 2 7l10 5 10-5-10-5z M2 17l10 5 10-5 M2 12l10 5 10-5");
        add("blocks", "M12 2 2 7l10 5 10-5-10-5z M2 17l10 5 10-5 M2 12l10 5 10-5");
        add("palettes",
            "M12 22a10 10 0 1 1 10-10c0 2.8-2.2 4-4 4h-2a2 2 0 0 0-1.5 3.3A2 2 0 0 1 12 22z"
            " M7.5 10.5h.01 M12 7.5h.01 M16.5 10.5h.01");
        add("sprites",
            "M3 3h18v18H3z M7 3v18 M17 3v18 M3 7.5h4 M3 12h18 M3 16.5h4 M17 7.5h4 M17 16.5h4");
        add("text", "M4 7V4h16v3 M9 20h6 M12 4v16");
        add("sound",
            "M9 18V5l12-2v13 M6 15a3 3 0 1 0 0 6a3 3 0 0 0 0-6z M18 13a3 3 0 1 0 0 6a3 3 0 0 0 0-6z");
        add("playtest", "M6 3l14 9-14 9z");
        add("chunks", "M3 3h18v18H3z M9 3v18 M15 3v18 M3 9h18 M3 15h18");
        add("collision", "M3 21h18 M3 21V15l6-6 4 4 8-8v16");
        // Editor tools.
        add("paint",
            "M9.06 11.9l8.07-8.06a2.85 2.85 0 1 1 4.03 4.03l-8.06 8.08 M7.07 14.94c-1.66 0-3 1.35"
            "-3 3.02 0 1.33-2.5 1.52-2 2.02 1.08 1.1 2.49 2.02 4 2.02 2.2 0 4-1.8 4-4.04a3.01 3.01"
            " 0 0 0-3-3.02z");
        add("select",
            "M4.037 4.688a.495.495 0 0 1 .651-.651l16 6.5a.5.5 0 0 1-.063.947l-6.124 1.58a2 2 0 0"
            " 0-1.438 1.435l-1.579 6.126a.5.5 0 0 1-.947.063z");
        add("move", "M12 2v20 M15 19l-3 3-3-3 M19 9l3 3-3 3 M2 12h20 M5 9l-3 3 3 3 M9 5l3-3 3 3");
        add("place", "M5 12h14 M12 5v14");
        add("erase",
            "M7 21l-4.3-4.3c-1-1-1-2.5 0-3.4l9.6-9.6c1-1 2.5-1 3.4 0l5.6 5.6c1 1 1 2.5 0 3.4L13 21"
            " M22 21H7 M5 11l9 9");
        add("pencil",
            "M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0"
            " 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497z");
        add("fill", "M19 11l-8-8-8.6 8.6a2 2 0 0 0 0 2.8l5.2 5.2c.8.8 2 .8 2.8 0L19 11z M5 2l5 5 M2 13h15");
        add("pick",
            "M2 22l1-1h3l9-9 M3 21v-3l9-9 M15 6l3.4-3.4a2.1 2.1 0 1 1 3 3L18 9l.4.4a2.1 2.1 0 1 1-3"
            " 3l-3.8-3.8a2.1 2.1 0 1 1 3-3l.4.4z");
        add("flip",
            "M8 3H5a2 2 0 0 0-2 2v14c0 1.1.9 2 2 2h3 M16 3h3a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2h-3"
            " M12 20v2 M12 14v2 M12 8v2 M12 2v2");
        // View and misc.
        add("grid", "M3 3h18v18H3z M3 9h18 M3 15h18 M9 3v18 M15 3v18");
        add("zoom-in", "M3 11a8 8 0 1 0 16 0a8 8 0 1 0 -16 0 M21 21l-4.3-4.3 M11 8v6 M8 11h6");
        add("zoom-out", "M3 11a8 8 0 1 0 16 0a8 8 0 1 0 -16 0 M21 21l-4.3-4.3 M8 11h6");
        add("eye",
            "M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75"
            " 10.75 0 0 1-19.876 0 M9 12a3 3 0 1 0 6 0a3 3 0 1 0-6 0");
        add("sun",
            "M8 12a4 4 0 1 0 8 0a4 4 0 1 0-8 0 M12 2v2 M12 20v2 M4.93 4.93l1.41 1.41 M17.66 17.66"
            "l1.41 1.41 M2 12h2 M20 12h2 M6.34 17.66l-1.41 1.41 M19.07 4.93l-1.41 1.41");
        add("moon", "M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9Z");
        add("settings",
            "M4 21v-7 M4 10V3 M12 21v-9 M12 8V3 M20 21v-5 M20 12V3 M1 14h6 M9 8h6 M17 16h6");
        add("folder",
            "M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3"
            "H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z");
        add("file", "M15 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7Z M14 2v4a2 2 0 0 0 2 2h4");
        add("warning",
            "m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3 M12 9v4"
            " M12 17h.01");
        add("info", "M2 12a10 10 0 1 0 20 0a10 10 0 1 0-20 0 M12 16v-4 M12 8h.01");
        add("error", "M2 12a10 10 0 1 0 20 0a10 10 0 1 0-20 0 M15 9l-6 6 M9 9l6 6");
        return t;
    }();
    return table;
}

QByteArray svgFor(const Glyph& glyph, const QColor& color) {
    const QString hex = color.name(QColor::HexRgb);
    const QString opacity = QString::number(color.alphaF(), 'f', 3);
    QString svg;
    if (glyph.filled) {
        svg = QStringLiteral(
                  "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                  "<path d='%1' fill='%2' fill-opacity='%3'/></svg>")
                  .arg(glyph.path, hex, opacity);
    } else {
        svg = QStringLiteral(
                  "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                  "<path d='%1' fill='none' stroke='%2' stroke-opacity='%3' stroke-width='1.5'"
                  " stroke-linecap='round' stroke-linejoin='round'/></svg>")
                  .arg(glyph.path, hex, opacity);
    }
    return svg.toUtf8();
}

QPixmap render(const QString& name, const QSize& deviceSize, const QColor& color) {
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1|%2x%3|%4")
                            .arg(name)
                            .arg(deviceSize.width())
                            .arg(deviceSize.height())
                            .arg(color.rgba(), 8, 16, QLatin1Char('0'));
    const auto hit = cache.constFind(key);
    if (hit != cache.constEnd()) {
        return *hit;
    }

    QPixmap pixmap(deviceSize);
    pixmap.fill(Qt::transparent);
    const auto glyph = glyphs().constFind(name);
    if (glyph != glyphs().constEnd() && !deviceSize.isEmpty()) {
        QSvgRenderer renderer(svgFor(*glyph, color));
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const int side = std::min(deviceSize.width(), deviceSize.height());
        const QRectF square((deviceSize.width() - side) / 2.0, (deviceSize.height() - side) / 2.0,
                            side, side);
        renderer.render(&painter, square);
    }
    cache.insert(key, pixmap);
    return pixmap;
}

QColor toneColor(IconTone tone, QIcon::Mode mode, QIcon::State state) {
    const Tokens& t = current();
    if (mode == QIcon::Disabled) {
        return t.faint;
    }
    const bool on = state == QIcon::On;
    switch (tone) {
    case IconTone::Muted:
        if (on || mode == QIcon::Selected) return t.accentInk;
        if (mode == QIcon::Active) return t.text;
        return t.muted;
    case IconTone::Rail:
        if (on || mode == QIcon::Active || mode == QIcon::Selected) return t.text;
        return t.muted;
    case IconTone::Text:
        if (mode == QIcon::Selected) return t.accentInk;
        return t.text;
    case IconTone::Faint:
        if (mode == QIcon::Active) return t.muted;
        if (mode == QIcon::Selected) return t.accentInk;
        return t.faint;
    case IconTone::Accent:
        return mode == QIcon::Active ? t.accentInk : t.accent;
    case IconTone::OnAccent:
        return t.onAccent;
    }
    return t.muted;
}

class GlyphEngine final : public QIconEngine {
public:
    GlyphEngine(QString name, IconTone tone) : name_(std::move(name)), tone_(tone) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        const double dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QSize device(static_cast<int>(std::lround(rect.width() * dpr)),
                           static_cast<int>(std::lround(rect.height() * dpr)));
        QPixmap pixmap = render(name_, device, toneColor(tone_, mode, state));
        pixmap.setDevicePixelRatio(dpr);
        painter->drawPixmap(rect.topLeft(), pixmap);
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return render(name_, size, toneColor(tone_, mode, state));
    }

    QIconEngine* clone() const override { return new GlyphEngine(name_, tone_); }

    QString key() const override { return QStringLiteral("rl-glyph"); }

private:
    QString name_;
    IconTone tone_;
};

}  // namespace

QIcon icon(const QString& name, IconTone tone) {
    return QIcon(new GlyphEngine(name, tone));
}

QPixmap iconPixmap(const QString& name, int logicalSize, const QColor& color,
                   double devicePixelRatio) {
    const int device = static_cast<int>(std::lround(logicalSize * devicePixelRatio));
    QPixmap pixmap = render(name, QSize(device, device), color);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    return pixmap;
}

bool hasIcon(const QString& name) {
    return glyphs().contains(name);
}

QStringList iconNames() {
    QStringList names = glyphs().keys();
    names.sort();
    return names;
}

void registerIcon(const QString& name, const QString& svgPathData, bool filled) {
    glyphs().insert(name, Glyph{svgPathData, filled});
}

}  // namespace rl::theme
