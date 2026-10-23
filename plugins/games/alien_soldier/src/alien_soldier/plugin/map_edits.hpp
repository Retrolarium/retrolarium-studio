#pragma once

#include "alien_soldier/data/documents.hpp"
#include "alien_soldier/data/scene.hpp"

#include <QJsonObject>
#include <QString>

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

// The document with one map value changed (throws when it cannot be).
void setMapValue(QJsonObject& document, const MapValue& value, int number);

}  // namespace rl::alien_soldier
