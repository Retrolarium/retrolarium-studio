#include "runtime/tool_locator.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace rl::runtime {

namespace {
constexpr int kParentDepth = 6;

QString python() {
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
    QStringList roots{QCoreApplication::applicationDirPath(), QDir::currentPath()};
    for (const QString& root : roots) {
        QDir directory(root);
        for (int depth = 0; depth <= kParentDepth; ++depth) {
            const QString candidate = directory.filePath(relative);
            if (QFileInfo(candidate).isFile()) {
                return QFileInfo(candidate).canonicalFilePath();
            }
            if (!directory.cdUp()) {
                break;
            }
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
        kit = findInParents(QStringLiteral("retrolarium_kit/rltool.py"));
    }
    if (kit.isEmpty()) {
        kit = QStandardPaths::findExecutable(QStringLiteral("rltool"));
    }
    if (kit.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Retrolarium Kit not found. Install retrolarium-kit, or set "
                                    "RETROLARIUM_KIT to rltool or rltool.py.");
        }
        return spec;
    }
    if (QFileInfo(kit).suffix().compare(QLatin1String("py"), Qt::CaseInsensitive) == 0) {
        const QString interpreter = python();
        if (interpreter.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Python is needed to run %1 but is not on PATH.").arg(kit);
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
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(file);
    return QFileInfo(path).isFile() ? path : QString();
}

}  // namespace rl::runtime
