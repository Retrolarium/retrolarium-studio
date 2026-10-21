#include "castle_excellent/data/levels.hpp"

#include "sdk/i18n.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPoint>
#include <QSaveFile>

#include <algorithm>

namespace rl::castle_excellent {

namespace {

[[noreturn]] void fail(const QString& message) {
    throw CastleExcellentError(message.toStdString());
}

QVector<int> ints(const QJsonValue& value) {
    QVector<int> result;
    for (const QJsonValue& item : value.toArray()) {
        result << item.toInt();
    }
    return result;
}

}  // namespace

CastleExcellentLevels CastleExcellentLevels::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(rlTr("castle_excellent", "Cannot read %1: %2").arg(path, file.errorString()));
    }
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &error);
    if (!json.isObject()) {
        fail(rlTr("castle_excellent", "%1 is not a JSON document: %2").arg(path, error.errorString()));
    }
    CastleExcellentLevels levels;
    levels.root_ = json.object();
    if (levels.root_.value(QStringLiteral("schema")).toString() != QLatin1String("retrolarium.castle-excellent-levels")) {
        fail(rlTr("castle_excellent", "%1 is not a Castle rooms document.").arg(path));
    }
    levels.width_ = levels.root_.value(QStringLiteral("width")).toInt(20);
    levels.height_ = levels.root_.value(QStringLiteral("height")).toInt(20);
    // A room fills two name tables at most.
    if (levels.width_ < 1 || levels.width_ > 30 || levels.height_ < 1 || levels.height_ > 23) {
        fail(rlTr("castle_excellent", "%1 has rooms of an impossible size.").arg(path));
    }
    return levels;
}

void CastleExcellentLevels::save(const QString& path) const {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(rlTr("castle_excellent", "Cannot write %1: %2").arg(path, file.errorString()));
    }
    file.write(QJsonDocument(root_).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        fail(rlTr("castle_excellent", "Cannot write %1: %2").arg(path, file.errorString()));
    }
}

int CastleExcellentLevels::roomCount() const {
    return root_.value(QStringLiteral("rooms")).toArray().size();
}

QString CastleExcellentLevels::revision() const {
    return root_.value(QStringLiteral("revision")).toString();
}

QJsonObject CastleExcellentLevels::room(int index) const {
    const QJsonArray rooms = root_.value(QStringLiteral("rooms")).toArray();
    if (index < 0 || index >= rooms.size()) {
        fail(rlTr("castle_excellent", "There is no room %1.").arg(index + 1));
    }
    return rooms.at(index).toObject();
}

void CastleExcellentLevels::setRoom(int index, const QJsonObject& room) {
    QJsonArray rooms = root_.value(QStringLiteral("rooms")).toArray();
    rooms.replace(index, room);
    root_.insert(QStringLiteral("rooms"), rooms);
}

int CastleExcellentLevels::roomNumber(int index) const {
    return room(index).value(QStringLiteral("number")).toInt(index + 1);
}

QVector<int> CastleExcellentLevels::cells(int index) const {
    QVector<int> result;
    result.reserve(width_ * height_);
    for (const QJsonValue& row : room(index).value(QStringLiteral("cells")).toArray()) {
        result += ints(row);
    }
    result.resize(width_ * height_);
    return result;
}

void CastleExcellentLevels::setCell(int index, QPoint cell, int value) {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= width_ || cell.y() >= height_) {
        fail(rlTr("castle_excellent", "The cell is outside the room."));
    }
    if (value < 0 || value >= std::max(1, renderingCellCount())) {
        fail(rlTr("castle_excellent", "There is no cell type %1.").arg(value));
    }
    QJsonObject r = room(index);
    QJsonArray rows = r.value(QStringLiteral("cells")).toArray();
    // Rows a hand-edited document leaves short read as empty cells: they
    // grow to the cell edited.
    while (rows.size() <= cell.y()) rows.append(QJsonArray());
    QJsonArray row = rows.at(cell.y()).toArray();
    while (row.size() <= cell.x()) row.append(0);
    row.replace(cell.x(), value);
    rows.replace(cell.y(), row);
    r.insert(QStringLiteral("cells"), rows);
    setRoom(index, r);
}

QVector<CastleExcellentObject> CastleExcellentLevels::objects(int index) const {
    QVector<CastleExcellentObject> result;
    for (const QJsonValue& value : room(index).value(QStringLiteral("objects")).toArray()) {
        const QJsonObject o = value.toObject();
        result << CastleExcellentObject{o.value(QStringLiteral("encoded_type")).toInt(), o.value(QStringLiteral("x")).toInt(),
                               o.value(QStringLiteral("y")).toInt()};
    }
    return result;
}

void CastleExcellentLevels::setObjects(int index, const QVector<CastleExcellentObject>& objects) {
    QJsonArray array;
    for (const CastleExcellentObject& o : objects) {
        if (o.x < 0 || o.y < 0 || o.x >= width_ || o.y >= height_) {
            fail(rlTr("castle_excellent", "An object would stand outside the room."));
        }
        if (o.type < 0 || o.type >= encodedTypeCount()) {
            fail(rlTr("castle_excellent", "There is no object type %1.").arg(o.type));
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

int CastleExcellentLevels::chrBank() const {
    return root_.value(QStringLiteral("rendering")).toObject().value(QStringLiteral("chr_bank")).toInt();
}

int CastleExcellentLevels::renderingCellCount() const {
    return root_.value(QStringLiteral("rendering")).toObject().value(QStringLiteral("left_tiles")).toArray().size();
}

QVector<int> CastleExcellentLevels::renderingInts(const QString& key) const {
    return ints(root_.value(QStringLiteral("rendering")).toObject().value(key));
}

int CastleExcellentLevels::encodedTypeCount() const {
    return renderingInts(QStringLiteral("runtime_object_types")).size();
}

QVector<CastleExcellentObjectPreview> CastleExcellentLevels::objectPreviews() const {
    QVector<CastleExcellentObjectPreview> result;
    for (const QJsonValue& value :
         root_.value(QStringLiteral("rendering")).toObject().value(QStringLiteral("object_previews")).toArray()) {
        const QJsonObject p = value.toObject();
        result << CastleExcellentObjectPreview{p.value(QStringLiteral("runtime_type")).toInt(),
                                      p.value(QStringLiteral("descriptor_offset")).toInt(),
                                      ints(p.value(QStringLiteral("x_offsets")))};
    }
    return result;
}

}  // namespace rl::castle_excellent
