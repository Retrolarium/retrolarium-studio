#include "gallery/theme_gallery.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QTimer>

namespace {

QSize parseSize(const QString& text, const QSize& fallback) {
    const QStringList parts = text.split(QLatin1Char('x'));
    if (parts.size() != 2) {
        return fallback;
    }
    bool okWidth = false;
    bool okHeight = false;
    const int width = parts.at(0).toInt(&okWidth);
    const int height = parts.at(1).toInt(&okHeight);
    return okWidth && okHeight && width > 0 && height > 0 ? QSize(width, height) : fallback;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    Q_INIT_RESOURCE(studio);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Retrolarium Studio"));
    QApplication::setOrganizationName(QStringLiteral("Retrolarium"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Retrolarium Studio"));
    parser.addHelpOption();
    const QCommandLineOption themeOption(QStringLiteral("theme"),
                                         QStringLiteral("Color theme: dark or light."),
                                         QStringLiteral("mode"), QStringLiteral("dark"));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Render the window off screen to a PNG and exit."), QStringLiteral("path"));
    const QCommandLineOption sizeOption(QStringLiteral("size"),
                                        QStringLiteral("Window size, e.g. 1600x1000."),
                                        QStringLiteral("WxH"), QStringLiteral("1600x1000"));
    const QCommandLineOption fontReportOption(
        QStringLiteral("font-report"), QStringLiteral("Write registered font families to a file."),
        QStringLiteral("path"));
    const QCommandLineOption switchOption(
        QStringLiteral("switch-theme"),
        QStringLiteral("With --screenshot: switch to this theme after the window is shown."),
        QStringLiteral("mode"));
    parser.addOptions({themeOption, screenshotOption, sizeOption, fontReportOption, switchOption});
    parser.process(app);

    rl::theme::Theme::instance().install(
        app, rl::theme::modeFromName(parser.value(themeOption), rl::theme::Mode::Dark));

    if (parser.isSet(fontReportOption)) {
        QFile report(parser.value(fontReportOption));
        if (report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            report.write(rl::theme::fontReport().toUtf8());
            report.write("\n");
        }
    }

    rl::gallery::ThemeGallery window;
    window.resize(parseSize(parser.value(sizeOption), QSize(1600, 1000)));

    if (parser.isSet(screenshotOption)) {
        const QString path = parser.value(screenshotOption);
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
        if (parser.isSet(switchOption)) {
            const auto target = rl::theme::modeFromName(parser.value(switchOption),
                                                        rl::theme::Theme::instance().mode());
            QTimer::singleShot(100, &app, [target] { rl::theme::Theme::instance().setMode(target); });
        }
        QTimer::singleShot(300, &app, [&window, path] {
            const bool saved = window.grab().save(path, "PNG");
            QApplication::exit(saved ? 0 : 1);
        });
        return app.exec();
    }

    window.show();
    return app.exec();
}
