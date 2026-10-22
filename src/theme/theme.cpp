#include "theme/theme.hpp"

#include "theme/industry_style.hpp"
#include "theme/stylesheet.hpp"
#include "theme/typography.hpp"
#include "theme/window_frame.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPalette>
#include <QWidget>

namespace rl::theme {

namespace {

QPalette paletteFor(const Tokens& t) {
    QPalette p;
    auto setAll = [&p](QPalette::ColorRole role, const QColor& color) {
        p.setColor(QPalette::Active, role, color);
        p.setColor(QPalette::Inactive, role, color);
        p.setColor(QPalette::Disabled, role, color);
    };
    setAll(QPalette::Window, t.chrome);
    setAll(QPalette::WindowText, t.text);
    setAll(QPalette::Base, t.field);
    setAll(QPalette::AlternateBase, t.panel);
    setAll(QPalette::Text, t.text);
    setAll(QPalette::Button, t.chrome);
    setAll(QPalette::ButtonText, t.text);
    setAll(QPalette::BrightText, t.accentInk);
    setAll(QPalette::Highlight, t.accentSoft);
    setAll(QPalette::HighlightedText, t.accentInk);
    setAll(QPalette::ToolTipBase, t.panel);
    setAll(QPalette::ToolTipText, t.text);
    setAll(QPalette::PlaceholderText, t.faint);
    setAll(QPalette::Link, t.accentInk);
    setAll(QPalette::LinkVisited, t.accentInk);
    setAll(QPalette::Mid, t.lineStrong);
    setAll(QPalette::Midlight, t.line);
    setAll(QPalette::Dark, t.bg);
    setAll(QPalette::Shadow, t.canvas);
    setAll(QPalette::Light, t.panel);
    p.setColor(QPalette::Disabled, QPalette::WindowText, t.faint);
    p.setColor(QPalette::Disabled, QPalette::Text, t.faint);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, t.faint);
    return p;
}

void writeText(const QString& path, const QString& text) {
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(text.toUtf8());
    }
}

QString strokeSvg(const QString& path, const QColor& color, double width) {
    return QStringLiteral(
               "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24'>"
               "<path d='%1' fill='none' stroke='%2' stroke-opacity='%3' stroke-width='%4'"
               " stroke-linecap='square' stroke-linejoin='miter'/></svg>")
        .arg(path, color.name(QColor::HexRgb), QString::number(color.alphaF(), 'f', 3), QString::number(width));
}

}  // namespace

Theme& Theme::instance() {
    static Theme theme;
    return theme;
}

void Theme::install(QApplication& app, Mode mode) {
    app_ = &app;
    mode_ = mode;
    loadFonts();
    assets_ = std::make_unique<QTemporaryDir>();
    writeAssets();
    app.setStyle(new IndustryStyle);
    app.setFont(font(TextRole::Body, size::kBody));
    apply();
}

void Theme::setMode(Mode mode) {
    if (mode == mode_) {
        return;
    }
    mode_ = mode;
    apply();
    emit changed(mode_);
}

void Theme::toggle() { setMode(mode_ == Mode::Dark ? Mode::Light : Mode::Dark); }

QString Theme::assetPath(const QString& name) const {
    if (!assets_ || !assets_->isValid()) {
        return QString();
    }
    // Forward slashes: the style sheet parser treats backslashes as escapes.
    return QDir::fromNativeSeparators(assets_->filePath(name));
}

void Theme::writeAssets() {
    if (!assets_ || !assets_->isValid()) {
        return;
    }
    // One file per mode and color: the style sheet engine caches images by
    // path, so rewriting a file in place would not repaint.
    for (Mode mode : {Mode::Dark, Mode::Light}) {
        const Tokens& t = theme::tokens(mode);
        const QString suffix = modeName(mode);
        writeText(assets_->filePath(QStringLiteral("chevron-down-%1.svg").arg(suffix)),
                  strokeSvg(QStringLiteral("M7 10l5 5 5-5"), t.faint, 2.0));
        writeText(assets_->filePath(QStringLiteral("chevron-up-%1.svg").arg(suffix)),
                  strokeSvg(QStringLiteral("M7 14l5-5 5 5"), t.faint, 2.0));
        writeText(assets_->filePath(QStringLiteral("check-%1.svg").arg(suffix)),
                  strokeSvg(QStringLiteral("M5 12.5l4.5 4.5L19 7.5"), t.onAccent, 3.0));
    }
}

void Theme::apply() {
    if (!app_) {
        return;
    }
    const Tokens& t = tokens();
    app_->setPalette(paletteFor(t));
    app_->setStyleSheet(buildStyleSheet(t, *this));
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->isWindow() && widget->testAttribute(Qt::WA_WState_Created)) {
            applyWindowFrame(widget, t);
        }
        widget->update();
    }
}

const Tokens& current() { return Theme::instance().tokens(); }

}  // namespace rl::theme
