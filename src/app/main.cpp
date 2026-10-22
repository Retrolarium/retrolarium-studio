#include "builtin_plugins.hpp"
#include "gallery/theme_gallery.hpp"
#include "shell/main_window.hpp"
#include "theme/brand.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include "frame/studio_dialog.hpp"
#include "sdk/i18n.hpp"
#include "shell/settings_dialog.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QScreen>
#include <QStyle>
#include <QTimer>
#include <QTranslator>

#include <exception>
#include <memory>

namespace {

// A safety net for an exception a plugin's model lets out of a slot or an
// event handler: unwinding through Qt's event loop would end the studio with
// every unsaved edit. It is reported once, so the user can save and restart.
class StudioApplication final : public QApplication {
public:
    using QApplication::QApplication;

    bool notify(QObject* receiver, QEvent* event) override {
        try {
            return QApplication::notify(receiver, event);
        } catch (const std::exception& e) {
            report(QString::fromUtf8(e.what()));
        } catch (...) {
            report(QString());
        }
        return false;
    }

private:
    void report(const QString& what) {
        qCritical("Unhandled exception: %s", qUtf8Printable(what));
        if (reported_) return;
        reported_ = true;
        // Not from inside the handler that threw (a paint, a mouse move).
        QTimer::singleShot(0, this, [what] {
            rl::frame::MessageDialog::inform(QApplication::activeWindow(), rlTr("studio", "Internal error"),
                                             rlTr("studio", "Something went wrong inside the studio"),
                                             rlTr("studio", "Save your work and restart the studio. Details: %1")
                                                 .arg(what.isEmpty() ? rlTr("studio", "unknown error") : what));
        });
    }

    bool reported_ = false;
};

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
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    Q_INIT_RESOURCE(studio);

    StudioApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Retrolarium Studio"));
    QApplication::setOrganizationName(QStringLiteral("Retrolarium"));
    QApplication::setApplicationVersion(QStringLiteral(RETROLARIUM_STUDIO_VERSION));

    // The interface language: the studio's own text, then Qt's dialogs.
    const QString language = rl::shell::SettingsDialog::language();
    rl::theme::setInterfaceLanguage(language);
    QTranslator studioTranslator;
    QTranslator qtTranslator;
    // English loads its file too: it holds the plural forms of counted text.
    if (studioTranslator.load(QStringLiteral(":/i18n/retrolarium_studio_%1").arg(language))) {
        QApplication::installTranslator(&studioTranslator);
        if (language != QLatin1String("en") && qtTranslator.load(QStringLiteral(":/i18n/qtbase_%1").arg(language))) {
            QApplication::installTranslator(&qtTranslator);
        }
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Retrolarium Studio"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption themeOption(QStringLiteral("theme"), QStringLiteral("Color theme: dark or light."),
                                         QStringLiteral("mode"), QStringLiteral("dark"));
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                              QStringLiteral("Render the window off screen to a PNG and exit."),
                                              QStringLiteral("path"));
    const QCommandLineOption sizeOption(QStringLiteral("size"),
                                        QStringLiteral("Window size, e.g. 1600x1000; without it the "
                                                       "window opens maximized."),
                                        QStringLiteral("WxH"), QStringLiteral("1600x1000"));
    const QCommandLineOption fontReportOption(QStringLiteral("font-report"),
                                              QStringLiteral("Write registered font families to a file."),
                                              QStringLiteral("path"));
    const QCommandLineOption switchOption(
        QStringLiteral("switch-theme"),
        QStringLiteral("With --screenshot: switch to this theme after the window is shown."), QStringLiteral("mode"));
    const QCommandLineOption galleryOption(QStringLiteral("gallery"),
                                           QStringLiteral("Show the theme gallery instead of the studio."));
    const QCommandLineOption documentOption(
        QStringLiteral("document"), QStringLiteral("Open kind/document after the project, e.g. levels/round-07."),
        QStringLiteral("kind/document"));
    parser.addOptions(
        {themeOption, screenshotOption, sizeOption, fontReportOption, switchOption, galleryOption, documentOption});
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("Project file (.rlj) to open."));
    parser.process(app);

    rl::plugins::registerBuiltinPlugins();

    rl::theme::Theme::instance().install(app,
                                         rl::theme::modeFromName(parser.value(themeOption), rl::theme::Mode::Dark));
    // The window and taskbar icon follow the theme: the accent tile in the
    // light one, the dark tile in the dark one.
    QApplication::setWindowIcon(rl::theme::brand::appIcon(rl::theme::Theme::instance().mode()));
    QObject::connect(&rl::theme::Theme::instance(), &rl::theme::Theme::changed, &app,
                     [](rl::theme::Mode mode) { QApplication::setWindowIcon(rl::theme::brand::appIcon(mode)); });

    if (parser.isSet(fontReportOption)) {
        QFile report(parser.value(fontReportOption));
        if (report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            report.write(rl::theme::fontReport().toUtf8());
            report.write("\n");
        }
    }

    std::unique_ptr<QWidget> owner;
    if (parser.isSet(galleryOption)) {
        owner = std::make_unique<rl::gallery::ThemeGallery>();
    } else {
        auto studio = std::make_unique<rl::shell::MainWindow>();
        if (!parser.positionalArguments().isEmpty()) {
            studio->openProjectFile(parser.positionalArguments().first());
            const QString document = parser.value(documentOption);
            if (!document.isEmpty()) {
                studio->showDocument(document.section(QLatin1Char('/'), 0, 0), document.section(QLatin1Char('/'), 1));
            }
        }
        owner = std::move(studio);
    }
    QWidget& window = *owner;
    // The studio fills the screen. Restoring it gives a window centred on the
    // screen; --size (screenshots, tests) asks for an exact size instead.
    const bool exactSize = parser.isSet(sizeOption) || parser.isSet(screenshotOption);
    if (exactSize) {
        window.resize(parseSize(parser.value(sizeOption), QSize(1600, 1000)));
    } else if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect room = screen->availableGeometry();
        const QSize size = QSize(1600, 1000).boundedTo(room.size() * 9 / 10);
        window.setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size, room));
    }

    if (parser.isSet(screenshotOption)) {
        const QString path = parser.value(screenshotOption);
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.setProperty("rlDrawActive", true);
        window.show();
        if (parser.isSet(switchOption)) {
            const auto target =
                rl::theme::modeFromName(parser.value(switchOption), rl::theme::Theme::instance().mode());
            QTimer::singleShot(100, &app, [target] { rl::theme::Theme::instance().setMode(target); });
        }
        QTimer::singleShot(300, &app, [&window, path] {
            const bool saved = window.grab().save(path, "PNG");
            QApplication::exit(saved ? 0 : 1);
        });
        return app.exec();
    }

    if (exactSize) {
        window.show();
    } else {
        window.showMaximized();
    }
    return app.exec();
}
