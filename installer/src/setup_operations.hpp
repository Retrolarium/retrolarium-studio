#pragma once

#include "installer_package.hpp"

#include <QString>
#include <QStringList>

#include <functional>

namespace rl::setup {

// The language chosen at setup's start (an interface language code): its
// text comes from the studio's translations, and the studio starts in it.
void setLanguage(const QString& code);
QString language();

enum class Mode { Install, Repair, Uninstall };

struct Request {
    Mode mode = Mode::Install;
    QString packagePath;
    PayloadManifest manifest;
    QString installDirectory;
    bool desktopShortcut = false;
    bool removeUserData = false;
};

using Progress = std::function<void(int percent, const QString& message)>;

QString defaultInstallDirectory();
QString installedDirectory();  // from the Installed Apps entry, or empty
QString installedVersion();
QString desktopShortcutPath();
// Where Open ROM downloads games and keeps their edits.
QString gamesDirectory();
// Studio programs (and the MAME and Python it runs) still running from
// `directory`, by file name.
QStringList runningPrograms(const QString& directory);
// Empty, missing, or an earlier installation: a directory setup may replace.
bool replaceableDirectory(const QString& directory);

bool install(const Request& request, const Progress& progress, QString* error);
bool uninstall(const Request& request, const Progress& progress, QString* error, QString* warning);

bool launchStudio(const QString& directory, QString* error);
// The uninstaller cannot delete itself while it runs: a hidden helper
// removes it and its directory once it has exited.
bool scheduleSelfRemoval(const QString& self, const QString& directory, QString* error);
bool sameFile(const QString& left, const QString& right);

extern const QString kUninstallerName;

}  // namespace rl::setup
