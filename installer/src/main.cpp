// Retrolarium Studio setup: one program that installs, repairs and removes
// the studio. Its payload (the package folder) follows the program itself;
// the same program, copied into the installation, is the uninstaller.
//
//   retrolarium-setup --create-package <output.exe> <package folder> <version>
//   retrolarium-setup --verify-package <setup.exe>
//   Uninstall.exe --uninstall

#include "installer_package.hpp"
#include "sdk/languages.hpp"
#include "setup_window.hpp"
#include "theme/brand.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QLocale>
#include <QTextStream>
#include <QTranslator>

int main(int argc, char* argv[]) {
    QStringList arguments;
    for (int i = 0; i < argc; ++i) arguments << QString::fromLocal8Bit(argv[i]);

    const int create = arguments.indexOf(QStringLiteral("--create-package"));
    if (create >= 0) {
        QCoreApplication app(argc, argv);
        if (create + 3 >= arguments.size()) return 2;
        QString error;
        if (!rl::setup::InstallerPackage::create(QCoreApplication::applicationFilePath(), arguments.at(create + 2),
                                                 arguments.at(create + 1), arguments.at(create + 3), &error)) {
            QTextStream(stderr) << error << '\n';
            return 3;
        }
        return 0;
    }
    const int verify = arguments.indexOf(QStringLiteral("--verify-package"));
    if (verify >= 0) {
        QCoreApplication app(argc, argv);
        if (verify + 1 >= arguments.size()) return 2;
        QString error;
        if (!rl::setup::InstallerPackage::verify(arguments.at(verify + 1), &error)) {
            QTextStream(stderr) << error << '\n';
            return 4;
        }
        return 0;
    }

    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    Q_INIT_RESOURCE(studio);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Retrolarium Studio Setup"));
    // Our titles already name the product: no " - <app>" suffix.
    QApplication::setApplicationDisplayName(QString());
    QApplication::setOrganizationName(QStringLiteral("Retrolarium"));

    // The language question itself is set for the system's language.
    rl::theme::setInterfaceLanguage(rl::systemInterfaceLanguage());
    rl::theme::Theme::instance().install(app, rl::theme::Mode::Dark);
    QApplication::setWindowIcon(rl::theme::brand::appIcon(rl::theme::Mode::Dark));

    // The studio's translations: setup's own text is among them.
    QTranslator studioTranslator;
    QTranslator qtTranslator;
    const auto use = [&](const QString& code) {
        QApplication::removeTranslator(&studioTranslator);
        QApplication::removeTranslator(&qtTranslator);
        const bool translated = code != QLatin1String("en")
                                && studioTranslator.load(QStringLiteral(":/i18n/retrolarium_studio_%1").arg(code));
        if (translated) {
            QApplication::installTranslator(&studioTranslator);
            if (qtTranslator.load(QStringLiteral(":/i18n/qtbase_%1").arg(code))) {
                QApplication::installTranslator(&qtTranslator);
            }
        }
        QLocale::setDefault(QLocale(code));
        rl::setup::setLanguage(code);
    };
    rl::setup::LanguageDialog language(use);
    if (language.exec() != QDialog::Accepted) return 0;
    // The faces of the chosen language, as the studio will use them.
    rl::theme::setInterfaceLanguage(language.chosen());
    QApplication::setFont(rl::theme::font(rl::theme::TextRole::Body, rl::theme::size::kBody));

    rl::setup::SetupWindow window(QCoreApplication::applicationFilePath(),
                                  arguments.contains(QStringLiteral("--uninstall"), Qt::CaseInsensitive));
    window.show();
    return app.exec();
}
