#pragma once

#include <retrolarium/nes_resource_document.hpp>

#include <QString>
#include <QVector>

namespace retrolarium {

struct SmbPositionedEnemy final {
    SmbEnemyRecord record;
    int index = 0;
    int page = 0;
    int x = 0;
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
};

struct SmbLevelScene final {
    // Column-major 13-row engine metatile buffer.
    QVector<QVector<int>> metatiles;
    QVector<SmbPositionedEnemy> enemies;
    int width = 0;
};

[[nodiscard]] SmbLevelScene renderSmbLevelScene(const QString &name,
    const QVector<int> &header, const QVector<SmbAreaObject> &objects,
    const QVector<SmbEnemyRecord> &enemies, int world = 0);

[[nodiscard]] SmbLevelScene renderSmbLevelScene(const QString &name,
    const QVector<int> &header, const QVector<SmbAreaObject> &objects,
    const QVector<SmbEnemyRecord> &enemies, int world,
    const SmbLevelRendering &rendering);

} // namespace retrolarium
