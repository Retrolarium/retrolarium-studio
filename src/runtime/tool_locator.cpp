#include "runtime/tool_locator.hpp"

#include "sdk/i18n.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace rl::runtime {

namespace {
constexpr int kParentDepth = 6;

QString python() {
    // The installed studio carries its own Python.
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("python/python.exe"));
    if (QFileInfo(bundled).isFile()) {
        return bundled;
    }
    for (const char* name : {"python", "python3", "py"}) {
        const QString found = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!found.isEmpty()) {
            return found;
        }
    }
    return {};
}
}  // namespace

QString ToolLocator::findInParents(const QString& relative) {
    // Only above the studio's own folder (a build inside the source tree),
    // never the current folder: a studio started there by a file association
    // must not run a kit or an emulator someone left in it.
    QDir directory(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth <= kParentDepth; ++depth) {
        const QString candidate = directory.filePath(relative);
        if (QFileInfo(candidate).isFile()) {
            return QFileInfo(candidate).canonicalFilePath();
        }
        if (!directory.cdUp()) {
            break;
        }
    }
    return {};
}

sdk::ProcessSpec ToolLocator::kitCommand(const QStringList& arguments, const QString& workingDirectory,
                                         QString* error) {
    sdk::ProcessSpec spec;
    spec.workingDirectory = workingDirectory;
    QString kit = qEnvironmentVariable("RETROLARIUM_KIT");
    if (!QFileInfo(kit).isFile()) {
        kit = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("kit/rltool.py"));
    }
    if (!QFileInfo(kit).isFile()) {
        kit = findInParents(QStringLiteral("retrolarium-kit/rltool.py"));
    }
    if (kit.isEmpty()) {
        kit = QStandardPaths::findExecutable(QStringLiteral("rltool"));
    }
    if (kit.isEmpty()) {
        if (error) {
            *error = rlTr("studio", "Retrolarium Kit was not found next to the studio. Reinstall "
                                    "Retrolarium Studio. (A studio built from source finds the kit "
                                    "through RETROLARIUM_KIT.)");
        }
        return spec;
    }
    if (QFileInfo(kit).suffix().compare(QLatin1String("py"), Qt::CaseInsensitive) == 0) {
        const QString interpreter = python();
        if (interpreter.isEmpty()) {
            if (error) {
                *error = rlTr("studio", "The Python that runs the kit was not found next to the "
                                        "studio. Reinstall Retrolarium Studio, or put Python on "
                                        "PATH to run %1.").arg(kit);
            }
            return spec;
        }
        spec.program = interpreter;
        spec.arguments << kit;
    } else {
        spec.program = kit;
    }
    spec.arguments += arguments;
    return spec;
}

QString ToolLocator::emulatorPath(const QString& id) {
    if (id != QLatin1String("mame")) {
        return {};
    }
    const QString configured = qEnvironmentVariable("RETROLARIUM_MAME");
    if (QFileInfo(configured).isFile()) {
        return QFileInfo(configured).canonicalFilePath();
    }
    const QString chosen = QSettings().value(QStringLiteral("tools/mame")).toString();
    if (QFileInfo(chosen).isFile()) {
        return QFileInfo(chosen).canonicalFilePath();
    }
    const QString bundled =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("mame/consoles.exe"));
    if (QFileInfo(bundled).isFile()) {
        return bundled;
    }
    return findInParents(QStringLiteral("mame_testing/mame/consoles.exe"));
}

QString ToolLocator::toolPath(const QString& name) {
    QString file = name;
#ifdef Q_OS_WIN
    if (!file.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) {
        file += QStringLiteral(".exe");
    }
#endif
    // Helper programs live in tools/ beside the studio, out of the user's way.
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools/") + file);
    return QFileInfo(path).isFile() ? path : QString();
}

}  // namespace rl::runtime
