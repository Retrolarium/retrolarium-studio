#pragma once

#include <QDir>
#include <QStringList>

namespace rl::setup {

// Pure path selection: tests never touch the registry or real user data.
inline QStringList userDataDirectories(bool removeData, const QString& documents, const QString& localData) {
    QStringList paths;
    if (!removeData) return paths;
    const auto append = [&](const QString& base, const QString& suffix) {
        if (!base.isEmpty() && QDir::isAbsolutePath(base) && !QDir(base).isRoot()) {
            paths << QDir(base).filePath(suffix);
        }
    };
    append(documents, QStringLiteral("Retrolarium"));
    append(localData, QStringLiteral("Retrolarium/Retrolarium Studio"));
    append(localData, QStringLiteral("Retrolarium/Kit/verified"));
    return paths;
}

}  // namespace rl::setup
