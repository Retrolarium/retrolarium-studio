#pragma once

#include <QString>
#include <QVector>

namespace rl::smb {

// Placeable area objects: the row a stream record needs (8 stands for any
// row 0-11) and its object control. The names are the game's (its
// repository's config/names.json), else the numbers.
struct SmbObjectKind {
    QString name;
    int row = 0;
    int control = 0;
};

struct SmbEnemyKind {
    int id = 0;
    QString name;
};

QVector<SmbObjectKind> objectCatalog();
QVector<SmbEnemyKind> enemyCatalog();

// What an area-object record is, or "Stream command $XX".
QString areaObjectName(int row, int control);
// An enemy by its object number, or "Enemy $XX".
QString enemyName(int id);

}  // namespace rl::smb
