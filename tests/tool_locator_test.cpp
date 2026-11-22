// ToolLocator: RETROLARIUM_KIT comes before every other place a kit is
// looked for, and on Windows only an .exe launcher is taken from PATH or
// from RETROLARIUM_KIT: a .cmd or .bat would run through cmd.exe, which Qt's
// quoting does not protect (a ROM file named "A & B.nes"). Needs no
// project; the kit-order part is skipped without a Python to run rltool.py.
//   tool_locator_test

#include "runtime/tool_locator.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

using rl::runtime::ToolLocator;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool touch(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write("@echo off\r\n");
    file.close();
    return file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
}

bool sameFile(const QString& a, const QString& b) {
    return !a.isEmpty() && QFileInfo(a).canonicalFilePath() == QFileInfo(b).canonicalFilePath();
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::cerr << "FAIL: no temporary folder\n";
        return 1;
    }

    const QByteArray previousDev = qgetenv("RETROLARIUM_DEV");
    qunsetenv("RETROLARIUM_DEV");
    // A regular installation must not discover a host Python or launcher.
    expect(ToolLocator::findOnPath(QStringLiteral("python")).isEmpty(), "host Python lookup needs dev mode");
    expect(ToolLocator::findOnPath(QStringLiteral("rltool")).isEmpty(), "host kit lookup needs dev mode");
    // RETROLARIUM_KIT first: the rltool.py it names is the one that runs.
    const QString script = dir.filePath(QStringLiteral("rltool.py"));
    expect(touch(script), "the kit script is written");
    qputenv("RETROLARIUM_KIT", QDir::toNativeSeparators(script).toLocal8Bit());
    QString error;
    const rl::sdk::ProcessSpec installed = ToolLocator::kitCommand({}, dir.path(), &error);
    const QString privatePython =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("python/python.exe"));
    if (QFileInfo(privatePython).isFile()) {
        expect(sameFile(installed.program, privatePython), "the private Python works without dev mode");
    } else {
        expect(installed.program.isEmpty() && !error.isEmpty(),
               "host interpreter discovery is disabled without dev mode");
    }
    qputenv("RETROLARIUM_DEV", "1");
    error.clear();
    const rl::sdk::ProcessSpec spec =
        ToolLocator::kitCommand({QStringLiteral("studio"), QStringLiteral("identify")}, dir.path(), &error);
    if (spec.program.isEmpty()) {
        std::cout << "kit order part skipped: " << error.toStdString() << '\n';
    } else {
        expect(spec.arguments.size() == 3, "the kit gets its script and the two arguments");
        expect(sameFile(spec.arguments.value(0), script), "RETROLARIUM_KIT names the kit that runs");
        expect(
            spec.arguments.value(1) == QLatin1String("studio") && spec.arguments.value(2) == QLatin1String("identify"),
            "the kit's arguments follow its script");
        expect(spec.workingDirectory == dir.path(), "the kit runs in the folder asked for");
    }

#ifdef Q_OS_WIN
    expect(!ToolLocator::isDirectLauncher(QStringLiteral("C:/kit/rltool.cmd")), "a .cmd needs cmd.exe");
    expect(!ToolLocator::isDirectLauncher(QStringLiteral("C:/kit/rltool.BAT")), "a .bat needs cmd.exe");
    expect(ToolLocator::isDirectLauncher(QStringLiteral("C:/kit/rltool.EXE")), "an .exe starts directly");

    // Only batch launchers on PATH: none is taken, and the one found is named.
    QTemporaryDir path;
    expect(path.isValid() && touch(path.filePath(QStringLiteral("rltool.cmd"))) &&
               touch(path.filePath(QStringLiteral("rltool.bat"))),
           "the batch launchers are written");
    const QByteArray savedPath = qgetenv("PATH");
    qputenv("PATH", QDir::toNativeSeparators(path.path()).toLocal8Bit());
    QString rejected;
    expect(ToolLocator::findOnPath(QStringLiteral("rltool"), &rejected).isEmpty(),
           "a .cmd or .bat on PATH is not taken");
    expect(!rejected.isEmpty(), "the launcher passed over is named");

    // With an .exe beside them, the .exe is the one.
    expect(touch(path.filePath(QStringLiteral("rltool.exe"))), "the .exe launcher is written");
    qunsetenv("RETROLARIUM_DEV");
    expect(ToolLocator::findOnPath(QStringLiteral("rltool")).isEmpty(), "even an exe on PATH needs explicit dev mode");
    qputenv("RETROLARIUM_DEV", "1");
    expect(sameFile(ToolLocator::findOnPath(QStringLiteral("rltool")), path.filePath(QStringLiteral("rltool.exe"))),
           "the .exe on PATH is taken");
    qputenv("PATH", savedPath);

    // RETROLARIUM_KIT naming a .cmd is refused as well.
    qputenv("RETROLARIUM_KIT", QDir::toNativeSeparators(path.filePath(QStringLiteral("rltool.cmd"))).toLocal8Bit());
    error.clear();
    const rl::sdk::ProcessSpec batch = ToolLocator::kitCommand({QStringLiteral("studio")}, path.path(), &error);
    expect(batch.program.isEmpty(), "a .cmd kit does not run");
    expect(!error.isEmpty(), "a .cmd kit is reported");
#else
    expect(ToolLocator::isDirectLauncher(QStringLiteral("/usr/local/bin/rltool")), "POSIX programs start directly");
#endif

    qunsetenv("RETROLARIUM_KIT");
    if (previousDev.isEmpty()) qunsetenv("RETROLARIUM_DEV");
    else qputenv("RETROLARIUM_DEV", previousDev);
    if (failures != 0) return 1;
    std::cout << "tool locator: kit order and launcher checks hold\n";
    return 0;
}
