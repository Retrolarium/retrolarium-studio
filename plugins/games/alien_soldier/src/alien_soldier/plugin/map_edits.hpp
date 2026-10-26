#pragma once

#include "alien_soldier/data/documents.hpp"
#include "alien_soldier/data/scene.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <stdexcept>

namespace rl::alien_soldier {

// Where a byte (or word) of a scene's RAM comes from: the map payload that
// last wrote it and the value inside it. `doc` is Maps or Screens; `index`
// is -1 when no editable document holds the payload.
struct MapValue {
    Doc doc = Doc::Maps;
    QString payload;
    int index = -1;
    int width = 1;  // 2 for pattern name words
};

MapValue mapValueAt(const QJsonObject& maps, const QJsonObject& screens, const Scene& scene, int offset);

// The bytes a map payload's values compress to (see data/lzss.hpp).
int mapPayloadSize(const QJsonArray& values, int width);

// A change after which the map no longer compresses into its slot.
struct MapOverflow : std::runtime_error {
    MapOverflow(QString payload, int size, int capacity)
        : std::runtime_error("the map does not fit its slot"),
          payload(std::move(payload)),
          size(size),
          capacity(capacity) {}
    QString payload;
    int size;
    int capacity;
};

// The document with one map value changed (throws when it cannot be: a
// MapOverflow when the map would no longer fit its slot, which the kit fills
// again when it builds).
void setMapValue(QJsonObject& document, const MapValue& value, int number);

}  // namespace rl::alien_soldier
