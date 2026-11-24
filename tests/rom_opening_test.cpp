// Open ROM keeps a usable fallback when Kit is unavailable or fails, asks
// before creating a project, reuses remembered projects, and respects cancel.
// All ROM bytes, settings and project files belong to this test's temp folder.
#include "frame/studio_dialog.hpp"
#include "rom/rom_plugin.hpp"
#include "runtime/tool_locator.hpp"
#include "sdk/plugin.hpp"
#include "shell/main_window.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <iostream>

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void runCase(const QString& mode, bool accept, bool close = false, bool repeat = false) {
    QTemporaryDir home;
    expect(home.isValid(), "the test home exists");
    qputenv("RETROLARIUM_STUDIO_HOME", home.path().toUtf8());
    qputenv("RETROLARIUM_ROM_TEST_MODE", mode.toUtf8());
    const QString marker = home.filePath(QStringLiteral("started"));
    qputenv("RETROLARIUM_ROM_TEST_MARKER", marker.toUtf8());
    QSettings().clear();

    const QString rom = home.filePath(QStringLiteral("example.nes"));
    QFile image(rom);
    QByteArray bytes(16 + 16384, '\0');
    bytes.replace(0, 4, QByteArray("NES\x1a", 4));
    bytes[4] = 1;
    expect(image.open(QIODevice::WriteOnly) && image.write(bytes) == bytes.size(), "a synthetic ROM is written");
    image.close();
    if (mode == QLatin1String("missing")) {
#ifdef Q_OS_WIN
        const QString kit = home.filePath(QStringLiteral("rltool.cmd"));
#else
        const QString kit = home.filePath(QStringLiteral("rltool.py"));
#endif
        QFile stub(kit);
        expect(stub.open(QIODevice::WriteOnly), "the unavailable Kit fixture is written");
        stub.close();
        qputenv("RETROLARIUM_KIT", kit.toUtf8());
        QString error;
        expect(rl::runtime::ToolLocator::kitCommand({}, home.path(), &error).program.isEmpty(),
               "Kit is unavailable in the fixture");
    } else {
        qputenv("RETROLARIUM_KIT", QCoreApplication::applicationFilePath().toUtf8());
    }

    const bool cancelled = mode == QLatin1String("wait");
    const bool unknown = mode == QLatin1String("unknown");
    for (int attempt = 0; attempt < (repeat ? 2 : 1); ++attempt) {
        rl::shell::MainWindow window;
        int questions = 0;
        bool finished = false;
        QTimer timer;
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            if (cancelled && QFile::exists(marker) && window.isBusy()) window.cancel();
            auto* dialog = qobject_cast<rl::frame::MessageDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            ++questions;
            if (close) {
                dialog->reject();
            } else {
                const QString answer = accept ? QStringLiteral("Open the ROM") : QStringLiteral("Cancel");
                for (auto* button : dialog->findChildren<QAbstractButton*>()) {
                    if (button->text() == answer) {
                        button->click();
                        return;
                    }
                }
                expect(false, "the fallback dialog has an explicit answer");
                dialog->reject();
            }
        });
        timer.start();
        QTimer::singleShot(8000, &window, [&] {
            finished = true;
            expect(false, "opening the ROM finishes in time");
            if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) modal->reject();
            window.cancel();
        });
        window.openRomFile(rom);
        QElapsedTimer elapsed;
        elapsed.start();
        while (!finished) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (!window.isBusy() && (questions || window.hasProject() || elapsed.elapsed() > 1500)) break;
            QThread::msleep(1);
        }
        timer.stop();
        expect(questions == (cancelled || unknown ? 0 : 1), "only a Kit failure asks once");
        const bool opened = unknown || (!cancelled && accept && !close);
        expect(window.hasProject() == opened, "only an accepted fallback or an unknown ROM opens a project");
        const QString root = home.filePath(QStringLiteral("Documents/Retrolarium/ROMs"));
        const QStringList folders = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        expect(folders.size() == (opened ? 1 : 0), "cancel creates nothing and remembered projects are reused");
    }
    std::cout << "checked " << mode.toStdString() << " accept=" << accept << " close=" << close << '\n';
}
}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && QByteArray(argv[1]) == "studio" && QByteArray(argv[2]) == "identify") {
        const QByteArray mode = qgetenv("RETROLARIUM_ROM_TEST_MODE");
        if (mode == "wait") {
            QFile marker(QString::fromUtf8(qgetenv("RETROLARIUM_ROM_TEST_MARKER")));
            if (!marker.open(QIODevice::WriteOnly)) return 2;
            marker.close();
            QThread::sleep(10);
        }
        if (mode == "failure") {
            std::cerr << "Kit identification failed\n";
            return 1;
        }
        std::cout << (mode == "unknown" ? "{\"matches\":[]}" : "invalid response");
        return 0;
    }
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QCoreApplication::setOrganizationName(QStringLiteral("RetrolariumTests"));
    QCoreApplication::setApplicationName(QStringLiteral("RomOpening"));
    qunsetenv("RETROLARIUM_DEV");
    qunsetenv("RETROLARIUM_TEST_HOOKS");
    rl::rom::registerPlugin(rl::sdk::PluginRegistry::instance());
    runCase(QStringLiteral("missing"), false);
    runCase(QStringLiteral("missing"), true, false, true);
    runCase(QStringLiteral("failure"), false);
    runCase(QStringLiteral("failure"), true);
    runCase(QStringLiteral("malformed"), true);
    runCase(QStringLiteral("malformed"), false, true);
    runCase(QStringLiteral("unknown"), true);
    runCase(QStringLiteral("wait"), false);
    return failures == 0 ? 0 : 1;
}
