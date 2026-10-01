#pragma once

#include "smb/data/level_records.hpp"

#include <QString>
#include <QStringList>
#include <QVector>

// Ported from the original Retrolarium app; the golden test pins its
// output to the kit's renderer for all SMB1 JU areas.

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

struct SmbLevelRendering final {
    QVector<int> terrainMetatiles;
    QVector<int> foregroundSceneryMetatiles;
    QVector<int> brickMetatiles;
    QVector<int> solidMetatiles;
    QVector<int> coinMetatiles;
    QVector<int> sidePipeTopMetatiles;
    QVector<int> sidePipeBottomMetatiles;
    QVector<int> castleSpecialMetatiles;
    QVector<int> secondaryLedgeMetatiles;
    QVector<int> secondaryLedgeSupportMetatiles;
    QVector<int> cannonMetatiles;
    QVector<int> flagpoleMetatiles;
    QVector<QVector<QVector<int>>> smallObjects;
    int bridgeMetatile = 0x63;
    int staircaseMetatile = 0x61;
    int residualFlagMetatile = 0x6d;
    bool upsideDownPipes = false;

    [[nodiscard]] static SmbLevelRendering smb1();
    [[nodiscard]] static SmbLevelRendering ann();
    [[nodiscard]] static SmbLevelRendering smb2();
    [[nodiscard]] static SmbLevelRendering fromId(const QString &id);
};

struct SmbLevelScene final {
    // Column-major 13-row engine metatile buffer.
    QVector<QVector<int>> metatiles;
    QVector<SmbPositionedEnemy> enemies;
    QVector<SmbPositionedObject> objects;
    int width = 0;
};

[[nodiscard]] SmbLevelScene renderSmbLevelScene(const QString &name,
    const QVector<int> &header, const QVector<SmbAreaObject> &objects,
    const QVector<SmbEnemyRecord> &enemies, int world = 0);

[[nodiscard]] SmbLevelScene renderSmbLevelScene(const QString &name,
    const QVector<int> &header, const QVector<SmbAreaObject> &objects,
    const QVector<SmbEnemyRecord> &enemies, int world,
    const SmbLevelRendering &rendering);

}  // namespace rl::smb
