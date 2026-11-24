#pragma once

#include <QDir>
#include <QStringList>

namespace rl::setup {

// Pure path selection: tests never touch the registry or real user data.
inline QString localUserDataDirectory(const QString& localData) {
    if (localData.isEmpty() || !QDir::isAbsolutePath(localData) || QDir(localData).isRoot()) return {};
    return QDir(localData).filePath(QStringLiteral("Retrolarium"));
}

inline QStringList userDataPaths(bool removeData, const QString& documents, const QString& localData) {
    QStringList paths;
    if (!removeData) return paths;
    const auto append = [&](const QString& base, const QString& suffix) {
        if (!base.isEmpty() && QDir::isAbsolutePath(base) && !QDir(base).isRoot()) {
            paths << QDir(base).filePath(suffix);
        }
    };
    append(documents, QStringLiteral("Retrolarium"));
    const QString local = localUserDataDirectory(localData);
    append(local, QStringLiteral("Retrolarium Studio"));
    append(local, QStringLiteral("Kit/verified"));
    append(local, QStringLiteral("settings.json"));
    return paths;
}

}  // namespace rl::setup
