#include "flicky/plugin/objects.hpp"

#include "flicky/data/level_document.hpp"
#include "sdk/i18n.hpp"

namespace rl::flicky::objects {

QStringList objectLayerFields() {
    QStringList fields;
    for (const QString& field : FlickyLevelDocument::objectFields()) {
        if (!field.startsWith(QLatin1String("background_group_"))) {
            fields << field;
        }
    }
    return fields;
}

QStringList backgroundLayerFields() {
    QStringList fields;
    for (const QString& field : FlickyLevelDocument::objectFields()) {
        if (field.startsWith(QLatin1String("background_group_"))) {
            fields << field;
        }
    }
    return fields;
}

QString title(const QString& field) {
    if (field == QLatin1String("player")) return rlTr("flicky", "Flicky / start");
    if (field == QLatin1String("entry_arrow")) return rlTr("flicky", "Entry arrow");
    if (field == QLatin1String("cat_door")) return rlTr("flicky", "Entry door");
    if (field == QLatin1String("exit_door")) return rlTr("flicky", "Exit door");
    if (field == QLatin1String("spawners")) return rlTr("flicky", "Collectible chicks");
    if (field == QLatin1String("chicks_a")) return rlTr("flicky", "Enemy group A");
    if (field == QLatin1String("chicks_b")) return rlTr("flicky", "Enemy group B");
    // The pictures each scenery group shows change with the round's art.
    if (field == QLatin1String("background_group_3")) return rlTr("flicky", "Windows");
    if (field == QLatin1String("background_group_4")) return rlTr("flicky", "Wall pictures (train, flowers, stars)");
    if (field == QLatin1String("background_group_5")) return rlTr("flicky", "Wall pictures (elephant, pyramids, sky)");
    if (field.startsWith(QLatin1String("background_group_"))) {
        return rlTr("flicky", "Scenery %1").arg(field.right(1));
    }
    if (field == QLatin1String("flag_7")) return rlTr("flicky", "Collision flag 7");
    if (field == QLatin1String("flag_7_6")) return rlTr("flicky", "Collision flag 7·6");
    if (field == QLatin1String("flag_5")) return rlTr("flicky", "Collision flag 5");
    return field;
}

QSize footprint(const QString& field) {
    if (field == QLatin1String("player")) return {3, 6};
    if (field == QLatin1String("entry_arrow") || field == QLatin1String("cat_door")) return {2, 2};
    if (field == QLatin1String("exit_door")) return {2, 3};
    if (field == QLatin1String("background_group_3")) return {2, 3};
    if (field == QLatin1String("background_group_4")) return {5, 3};
    if (field == QLatin1String("background_group_5")) return {4, 4};
    if (field == QLatin1String("spawners")) return {2, 2};
    if (field == QLatin1String("chicks_a") || field == QLatin1String("chicks_b")) return {3, 4};
    return {1, 1};
}

bool isSingle(const QString& field) {
    return field == QLatin1String("player") || field == QLatin1String("entry_arrow") ||
           field == QLatin1String("cat_door") || field == QLatin1String("exit_door");
}

bool isVariable(const QString& field) { return FlickyLevelDocument::isVariableObjectField(field); }

QStringList typeCandidates(const QString& field) {
    if (!isVariable(field)) {
        return {field};
    }
    const bool special = FlickyLevelDocument::isSpecialObjectField(field);
    QStringList result;
    for (const QString& other : FlickyLevelDocument::objectFields()) {
        if (isVariable(other) && FlickyLevelDocument::isSpecialObjectField(other) == special) {
            result << other;
        }
    }
    return result;
}

QString encoding(const QString& field) {
    if (isSingle(field)) {
        return rlTr("flicky", "2 bytes: x, y");
    }
    if (field == QLatin1String("spawners")) {
        return rlTr("flicky", "6 × 2 bytes: x, y");
    }
    return rlTr("flicky", "count + 2 bytes per item");
}

}  // namespace rl::flicky::objects
