#include "flicky/plugin/objects.hpp"

#include "flicky/data/level_document.hpp"

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
    if (field == QLatin1String("player")) return QStringLiteral("Flicky / start");
    if (field == QLatin1String("entry_arrow")) return QStringLiteral("Entry arrow");
    if (field == QLatin1String("cat_door")) return QStringLiteral("Entry door");
    if (field == QLatin1String("exit_door")) return QStringLiteral("Exit door");
    if (field == QLatin1String("spawners")) return QStringLiteral("Collectible chicks");
    if (field == QLatin1String("chicks_a")) return QStringLiteral("Enemy group A");
    if (field == QLatin1String("chicks_b")) return QStringLiteral("Enemy group B");
    if (field.startsWith(QLatin1String("background_group_"))) {
        return QStringLiteral("Scenery %1").arg(field.right(1));
    }
    if (field == QLatin1String("flag_7")) return QStringLiteral("Collision flag 7");
    if (field == QLatin1String("flag_7_6")) return QStringLiteral("Collision flag 7·6");
    if (field == QLatin1String("flag_5")) return QStringLiteral("Collision flag 5");
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

bool isVariable(const QString& field) {
    return FlickyLevelDocument::isVariableObjectField(field);
}

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
        return QStringLiteral("2 bytes: x, y");
    }
    if (field == QLatin1String("spawners")) {
        return QStringLiteral("6 × 2 bytes: x, y");
    }
    return QStringLiteral("count + 2 bytes per item");
}

}  // namespace rl::flicky::objects
