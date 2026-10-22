#pragma once

#include "smb/data/level_records.hpp"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

// The kit's level renderer in C++; the golden test pins both to the same
// scenes for all SMB1 JU areas. It follows the area parser's logic, and
// every metatile it lays out comes from the project's level-tables
// resource, which the kit reads from the game's own build.

namespace rl::smb {

struct SmbPositionedEnemy final {
    SmbEnemyRecord record;
    int index = 0;
    int page = 0;
    int x = 0;
};

struct SmbPositionedObject final {
    SmbAreaObject record;
    int index = 0;
    int page = 0;
    int x = 0;
    int width = 1;
};

// The bundle resource of the level tables.
inline const QString kLevelTables = QStringLiteral("level-tables");

// What one engine renders with: its id (smb1, ann or smb2) and the tables
// of the project's level-tables resource (codec nes.byte-tables.v1).
struct SmbLevelRendering final {
    QString engine;
    QHash<QString, QVector<int>> tables;

    // Throws SmbError when the engine is unknown or a table is missing.
    [[nodiscard]] static SmbLevelRendering fromResource(const QJsonObject& resource, const QString& engine);

    [[nodiscard]] bool has(const QString& name) const { return tables.contains(name); }
    [[nodiscard]] const QVector<int>& table(const QString& name) const;
    [[nodiscard]] int one(const QString& name) const { return table(name).at(0); }
    // The metatiles of row object value (identifier 0), top down.
    [[nodiscard]] QVector<int> smallObject(int value, int areaType) const;
    // A sideways pipe's "top" or "bottom" row, left to right.
    [[nodiscard]] QVector<int> sidePipe(const QString& part) const;
};

struct SmbLevelScene final {
    // Column-major 13-row engine metatile buffer.
    QVector<QVector<int>> metatiles;
    QVector<SmbPositionedEnemy> enemies;
    QVector<SmbPositionedObject> objects;
    int width = 0;
};

[[nodiscard]] SmbLevelScene renderSmbLevelScene(const QString& name, const QVector<int>& header,
                                                const QVector<SmbAreaObject>& objects,
                                                const QVector<SmbEnemyRecord>& enemies, int world,
                                                const SmbLevelRendering& rendering);

}  // namespace rl::smb
