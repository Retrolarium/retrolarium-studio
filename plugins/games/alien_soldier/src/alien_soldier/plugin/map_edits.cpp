#include "alien_soldier/plugin/map_edits.hpp"

#include <QJsonArray>

#include <stdexcept>

namespace rl::alien_soldier {

MapValue mapValueAt(const QJsonObject& maps, const QJsonObject& screens, const Scene& scene, int offset) {
    MapValue value;
    const int writer = scene.writerOf(offset);
    if (writer < 0) return value;
    const RamWrite& write = scene.writes[writer];
    value.payload = write.source;
    for (Doc doc : {Doc::Maps, Doc::Screens}) {
        const QJsonArray payloads = (doc == Doc::Maps ? maps : screens).value(QStringLiteral("payloads")).toArray();
        const int found = indexOfLabel(payloads, write.source);
        if (found < 0) continue;
        const QJsonObject payload = payloads.at(found).toObject();
        value.doc = doc;
        value.width = payload.value(QStringLiteral("role")).toString() == QLatin1String("patterns") ? 2 : 1;
        const int within = offset - write.offset;
        if (within % value.width == 0 &&
            within / value.width < payload.value(QStringLiteral("values")).toArray().size()) {
            value.index = within / value.width;
        }
        return value;
    }
    return value;
}

void setMapValue(QJsonObject& document, const MapValue& value, int number) {
    QJsonArray payloads = document.value(QStringLiteral("payloads")).toArray();
    const int found = indexOfLabel(payloads, value.payload);
    if (found < 0 || value.index < 0) throw std::runtime_error("no document holds this map value");
    if (number < 0 || number >= (1 << (8 * value.width))) throw std::runtime_error("the value does not fit its map");
    QJsonObject payload = payloads.at(found).toObject();
    QJsonArray values = payload.value(QStringLiteral("values")).toArray();
    values.replace(value.index, number);
    payload.insert(QStringLiteral("values"), values);
    payloads.replace(found, payload);
    document.insert(QStringLiteral("payloads"), payloads);
}

}  // namespace rl::alien_soldier
