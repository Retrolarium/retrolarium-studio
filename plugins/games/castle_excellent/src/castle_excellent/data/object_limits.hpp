#pragma once

#include "castle_excellent/data/levels.hpp"

#include <QJsonArray>

namespace rl::castle_excellent {

struct CastleExcellentObjectLimits {
    // Older projects without a repository profile use the retail layout.
    int streamBytes = 2449;
    int stateBytes = 141;
    int perRoom = 24;

    static CastleExcellentObjectLimits fromProfile(const QJsonObject& profile, const QString& revision) {
        if (profile.value(QStringLiteral("id")).toString() != QLatin1String("castle-excellent")) {
            throw CastleExcellentError("Expected a Castle Excellent game profile.");
        }
        QJsonArray resources = profile.value(QStringLiteral("resources")).toArray();
        QJsonArray overrides;
        bool found = false;
        for (const QJsonValue& value : profile.value(QStringLiteral("revisions")).toArray()) {
            const QJsonObject selected = value.toObject();
            if (selected.value(QStringLiteral("id")).toString() != revision) continue;
            found = true;
            if (selected.contains(QStringLiteral("resources"))) {
                resources = selected.value(QStringLiteral("resources")).toArray();
            } else {
                overrides = selected.value(QStringLiteral("resource_overrides")).toArray();
            }
            break;
        }
        if (!found) throw CastleExcellentError("Unknown Castle Excellent profile revision.");
        QJsonObject parameters;
        for (const QJsonValue& value : resources) {
            const QJsonObject resource = value.toObject();
            if (resource.value(QStringLiteral("id")).toString() == QLatin1String("objects")) {
                parameters = resource.value(QStringLiteral("parameters")).toObject();
                break;
            }
        }
        if (parameters.isEmpty()) throw CastleExcellentError("The profile has no object resource.");
        for (const QJsonValue& value : overrides) {
            const QJsonObject resource = value.toObject();
            if (resource.value(QStringLiteral("id")).toString() != QLatin1String("objects")) continue;
            const QJsonObject patch = resource.value(QStringLiteral("parameters")).toObject();
            for (auto it = patch.begin(); it != patch.end(); ++it) parameters.insert(it.key(), it.value());
        }
        auto integer = [&](const char* name, int minimum, int maximum) {
            const QJsonValue value = parameters.value(QString::fromLatin1(name));
            const int number = value.toInt(-1);
            if (!value.isDouble() || value.toDouble() != number || number < minimum || number > maximum) {
                throw CastleExcellentError("Invalid Castle Excellent object capacity.");
            }
            return number;
        };
        const int first = integer("stream_cpu", 0x8000, 0xFFFF);
        const int end = integer("stream_end_cpu", first + 1, 0x10000);
        return {end - first, integer("state_capacity", 1, 255), integer("maximum_per_room", 1, 24)};
    }
};

}  // namespace rl::castle_excellent
