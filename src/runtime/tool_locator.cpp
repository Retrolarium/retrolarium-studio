#include "runtime/tool_locator.hpp"

#include "sdk/i18n.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QVersionNumber>

#include <algorithm>

namespace rl::runtime {

namespace {
constexpr int kParentDepth = 6;

// The kit's user settings (core/user_settings.py): RETROLARIUM_SETTINGS, else
// Retrolarium/settings.json under LOCALAPPDATA on Windows, else
// retrolarium/settings.json under XDG_CONFIG_HOME or ~/.config.
QString kitSetting(const QString& name) {
    QString path = qEnvironmentVariable("RETROLARIUM_SETTINGS");
    if (path.isEmpty()) {
#ifdef Q_OS_WIN
        const QString local = qEnvironmentVariable("LOCALAPPDATA");
        if (!local.isEmpty()) path = QDir(local).filePath(QStringLiteral("Retrolarium/settings.json"));
#endif
    }
    if (path.isEmpty()) {
        const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
        path = QDir(config.isEmpty() ? QDir::home().filePath(QStringLiteral(".config")) : config)
                   .filePath(QStringLiteral("retrolarium/settings.json"));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(name).toString();
}

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
#ifdef Q_OS_WIN
    // A studio started from a program that holds an older PATH (an editor,
    // a shell opened before Python was installed) does not see Python on
    // it: the installer's registry entries and its default folders still
    // name it. The newest version first.
    QStringList candidates;
    for (const char* root : {"HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE"}) {
        QSettings core(QStringLiteral("%1\\Software\\Python\\PythonCore").arg(QString::fromLatin1(root)),
                       QSettings::NativeFormat);
        QStringList versions = core.childGroups();
        std::sort(versions.begin(), versions.end(), [](const QString& a, const QString& b) {
            return QVersionNumber::fromString(a) > QVersionNumber::fromString(b);
        });
        for (const QString& version : versions) {
            const QString executable = core.value(version + QStringLiteral("/InstallPath/ExecutablePath")).toString();
            const QString folder = core.value(version + QStringLiteral("/InstallPath/Default")).toString();
            candidates << executable << (folder.isEmpty() ? QString() : QDir(folder).filePath(QStringLiteral("python.exe")));
        }
    }
    QDir programs(QDir(qEnvironmentVariable("LOCALAPPDATA")).filePath(QStringLiteral("Programs/Python")));
    QStringList folders = programs.entryList({QStringLiteral("Python3*")}, QDir::Dirs, QDir::Name | QDir::Reversed);
    for (const QString& folder : folders) {
        candidates << programs.filePath(folder + QStringLiteral("/python.exe"));
    }
    candidates << QStringLiteral("C:/Windows/py.exe");
    for (const QString& candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo(candidate).isFile()) {
            return QDir::toNativeSeparators(candidate);
        }
    }
#endif
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
    // The kit runs the core's codecs (Nemesis, Enigma) as rlcore: the one
    // the studio ships, unless the environment names another.
    const QString core = toolPath(QStringLiteral("rlcore"));
    if (!core.isEmpty() && qEnvironmentVariableIsEmpty("RETROLARIUM_CORE")) {
        spec.environment.insert(QStringLiteral("RETROLARIUM_CORE"), core);
    }
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
    const QString remembered = kitSetting(QStringLiteral("mame"));
    if (!remembered.isEmpty() && QFileInfo(remembered).isFile()) {
        return QFileInfo(remembered).canonicalFilePath();
    }
    const QString bundled =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("mame/consoles.exe"));
    if (QFileInfo(bundled).isFile()) {
        return bundled;
    }
    return findInParents(QStringLiteral("emulators/mame/mame/consoles.exe"));
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
