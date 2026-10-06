#include "castle/data/levels.hpp"

#include "sdk/i18n.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPoint>
#include <QSaveFile>

#include <algorithm>

namespace rl::castle {

namespace {

[[noreturn]] void fail(const QString& message) {
    throw CastleError(message.toStdString());
}

QVector<int> ints(const QJsonValue& value) {
    QVector<int> result;
    for (const QJsonValue& item : value.toArray()) {
        result << item.toInt();
    }
    return result;
}

}  // namespace

CastleLevels CastleLevels::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(rlTr("castle", "Cannot read %1: %2").arg(path, file.errorString()));
    }
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &error);
    if (!json.isObject()) {
        fail(rlTr("castle", "%1 is not a JSON document: %2").arg(path, error.errorString()));
    }
    CastleLevels levels;
    levels.root_ = json.object();
    if (levels.root_.value(QStringLiteral("schema")).toString() != QLatin1String("retrolarium.castle-levels")) {
        fail(rlTr("castle", "%1 is not a Castle rooms document.").arg(path));
    }
    levels.width_ = levels.root_.value(QStringLiteral("width")).toInt(20);
    levels.height_ = levels.root_.value(QStringLiteral("height")).toInt(20);
    return levels;
}

void CastleLevels::save(const QString& path) const {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(rlTr("castle", "Cannot write %1: %2").arg(path, file.errorString()));
    }
    file.write(QJsonDocument(root_).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        fail(rlTr("castle", "Cannot write %1: %2").arg(path, file.errorString()));
    }
}

int CastleLevels::roomCount() const {
    return root_.value(QStringLiteral("rooms")).toArray().size();
}

QString CastleLevels::revision() const {
    return root_.value(QStringLiteral("revision")).toString();
}

QJsonObject CastleLevels::room(int index) const {
    const QJsonArray rooms = root_.value(QStringLiteral("rooms")).toArray();
    if (index < 0 || index >= rooms.size()) {
        fail(rlTr("castle", "There is no room %1.").arg(index + 1));
    }
    return rooms.at(index).toObject();
}

void CastleLevels::setRoom(int index, const QJsonObject& room) {
    QJsonArray rooms = root_.value(QStringLiteral("rooms")).toArray();
    rooms.replace(index, room);
    root_.insert(QStringLiteral("rooms"), rooms);
}

int CastleLevels::roomNumber(int index) const {
    return room(index).value(QStringLiteral("number")).toInt(index + 1);
}

QVector<int> CastleLevels::cells(int index) const {
    QVector<int> result;
    result.reserve(width_ * height_);
    for (const QJsonValue& row : room(index).value(QStringLiteral("cells")).toArray()) {
        result += ints(row);
    }
    result.resize(width_ * height_);
    return result;
}

void CastleLevels::setCell(int index, QPoint cell, int value) {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= width_ || cell.y() >= height_) {
        fail(rlTr("castle", "The cell is outside the room."));
    }
    if (value < 0 || value >= std::max(1, renderingCellCount())) {
        fail(rlTr("castle", "There is no cell type %1.").arg(value));
    }
    QJsonObject r = room(index);
    QJsonArray rows = r.value(QStringLiteral("cells")).toArray();
    QJsonArray row = rows.at(cell.y()).toArray();
    row.replace(cell.x(), value);
    rows.replace(cell.y(), row);
    r.insert(QStringLiteral("cells"), rows);
    setRoom(index, r);
}

QVector<CastleObject> CastleLevels::objects(int index) const {
    QVector<CastleObject> result;
    for (const QJsonValue& value : room(index).value(QStringLiteral("objects")).toArray()) {
        const QJsonObject o = value.toObject();
        result << CastleObject{o.value(QStringLiteral("encoded_type")).toInt(), o.value(QStringLiteral("x")).toInt(),
                               o.value(QStringLiteral("y")).toInt()};
    }
    return result;
}

void CastleLevels::setObjects(int index, const QVector<CastleObject>& objects) {
    QJsonArray array;
    for (const CastleObject& o : objects) {
        if (o.x < 0 || o.y < 0 || o.x >= width_ || o.y >= height_) {
            fail(rlTr("castle", "An object would stand outside the room."));
        }
        if (o.type < 0 || o.type >= encodedTypeCount()) {
            fail(rlTr("castle", "There is no object type %1.").arg(o.type));
        }
        QJsonObject json;
        json.insert(QStringLiteral("encoded_type"), o.type);
        json.insert(QStringLiteral("x"), o.x);
        json.insert(QStringLiteral("y"), o.y);
        array.append(json);
    }
    QJsonObject r = room(index);
    r.insert(QStringLiteral("objects"), array);
    setRoom(index, r);
}

int CastleLevels::chrBank() const {
    return root_.value(QStringLiteral("rendering")).toObject().value(QStringLiteral("chr_bank")).toInt();
}

int CastleLevels::renderingCellCount() const {
    return root_.value(QStringLiteral("rendering")).toObject().value(QStringLiteral("left_tiles")).toArray().size();
}

QVector<int> CastleLevels::renderingInts(const QString& key) const {
    return ints(root_.value(QStringLiteral("rendering")).toObject().value(key));
}

int CastleLevels::encodedTypeCount() const {
    return renderingInts(QStringLiteral("runtime_object_types")).size();
}

QVector<CastleObjectPreview> CastleLevels::objectPreviews() const {
    QVector<CastleObjectPreview> result;
    for (const QJsonValue& value :
         root_.value(QStringLiteral("rendering")).toObject().value(QStringLiteral("object_previews")).toArray()) {
        const QJsonObject p = value.toObject();
        result << CastleObjectPreview{p.value(QStringLiteral("runtime_type")).toInt(),
                                      p.value(QStringLiteral("descriptor_offset")).toInt(),
                                      ints(p.value(QStringLiteral("x_offsets")))};
    }
    return result;
}

}  // namespace rl::castle
