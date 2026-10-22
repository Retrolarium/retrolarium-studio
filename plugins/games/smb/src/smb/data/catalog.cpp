#include "smb/data/catalog.hpp"

#include "sdk/game_names.hpp"

#include <QMap>

namespace rl::smb {

namespace {
QString hex(int value) { return QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper(); }

// The placeable area objects: the row a record needs (8 stands for rows
// 0-11) and the object control, in the order the add list shows them.
struct ObjectSlot {
    int row;
    int control;
};
const ObjectSlot kObjects[] = {
    {8, 0x00},  {8, 0x01},  {8, 0x02},  {8, 0x03},  {8, 0x04},  {8, 0x05},  {8, 0x06},  {8, 0x07},
    {8, 0x08},  {8, 0x09},  {8, 0x0a},  {8, 0x0b},  {8, 0x10},  {8, 0x20},  {8, 0x30},  {8, 0x40},
    {8, 0x50},  {8, 0x60},  {8, 0x70},  {12, 0x00}, {12, 0x10}, {12, 0x20}, {12, 0x30}, {12, 0x40},
    {12, 0x50}, {12, 0x60}, {12, 0x70}, {13, 0x40}, {13, 0x41}, {13, 0x42}, {13, 0x43}, {13, 0x44},
    {13, 0x45}, {13, 0x46}, {13, 0x47}, {13, 0x48}, {13, 0x49}, {13, 0x4a}, {13, 0x4b},
};

}  // namespace

// The game's names come from its repository (sdk::GameNames): area_objects by
// "row:control", enemies by id; without them an entry shows its numbers.
QString objectKindName(int row, int control) {
    const QString key = QStringLiteral("%1:%2").arg(row).arg(hex(control));
    return sdk::gameName("area_objects", key, QStringLiteral("Object %1").arg(key));
}

QVector<SmbObjectKind> objectCatalog() {
    QVector<SmbObjectKind> result;
    for (const ObjectSlot& slot : kObjects) {
        result.append({objectKindName(slot.row, slot.control), slot.row, slot.control});
    }
    return result;
}

// The enemies a level's stream can place are the ones the game names (its
// names.json leaves out ids that place nothing, such as one without an
// initializer).
QVector<SmbEnemyKind> enemyCatalog() {
    QVector<SmbEnemyKind> result;
    const QMap<int, QString> names = sdk::gameNames("enemies");
    for (auto it = names.begin(); it != names.end(); ++it) {
        result.append({it.key(), it.value()});
    }
    return result;
}

QString areaObjectName(int row, int control) {
    if (row <= 11) {
        const int family = (control >> 4) & 7;
        if (family == 0 && (control & 15) < 12) {
            return objectKindName(8, control & 15);
        }
        if (family >= 1) {
            return objectKindName(8, family << 4);
        }
    } else if (row == 12) {
        return objectKindName(12, control & 0x70);
    } else if (row == 13 && (control & 0x40) && (control & 0x3f) < 12) {
        return objectKindName(13, 0x40 | (control & 0x3f));
    }
    return QStringLiteral("Stream command $%1").arg(hex(control));
}

QString enemyName(int id) { return sdk::gameName("enemies", id, QStringLiteral("Enemy $%1").arg(hex(id))); }

}  // namespace rl::smb
