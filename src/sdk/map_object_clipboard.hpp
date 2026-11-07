#pragma once

#include <QJsonObject>
#include <QPoint>
#include <QString>

namespace rl::sdk {

// Optional object copying for map models. The model owns the payload schema
// and finds an empty position near preferredCell. (-1,-1) uses the copied
// object's original position. A successful paste is one undoable edit.
class MapObjectClipboard {
public:
    virtual ~MapObjectClipboard() = default;
    virtual QJsonObject copyMapObject(const QString& id) const = 0;
    virtual bool pasteMapObject(const QJsonObject& object, QPoint preferredCell, QString* error) = 0;
};

}  // namespace rl::sdk
