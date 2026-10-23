#include "alien_soldier/plugin/levels_model.hpp"

#include "alien_soldier/plugin/map_edits.hpp"
#include "alien_soldier/plugin/session.hpp"
#include "sdk/game_names.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>
#include <stdexcept>

namespace rl::alien_soldier {

namespace {

const QString kObjects = QStringLiteral("objects");
const QString kChunks = QStringLiteral("chunks");
const QStringList kPlacementFields = {QStringLiteral("x"),          QStringLiteral("y"),
                                      QStringLiteral("type"),       QStringLiteral("parameter"),
                                      QStringLiteral("slot_first"), QStringLiteral("slot_limit")};
const QStringList kRecordFields = {
    QStringLiteral("camera_x"),           QStringLiteral("camera_y"),        QStringLiteral("secondary_camera_x"),
    QStringLiteral("secondary_camera_y"), QStringLiteral("player_x"),        QStringLiteral("player_y"),
    QStringLiteral("sprite_priority"),    QStringLiteral("plane_a_mode"),    QStringLiteral("plane_b_mode"),
    QStringLiteral("spawn_director"),     QStringLiteral("palette_primary"), QStringLiteral("palette_secondary")};

QString hex(int value, int digits) { return QStringLiteral("$%1").arg(value, digits, 16, QLatin1Char('0')).toUpper(); }

int placementIndex(const QString& id) { return id.startsWith(QLatin1Char('p')) ? id.mid(1).toInt() : -1; }

// The name of an object type: the game's (config/names.json, objects) or
// its handler's label.
QString typeName(const QJsonObject& levels, int type) {
    const QString key = hex(type & ~3, 1);
    const QString handler = levels.value(QStringLiteral("handlers")).toObject().value(key).toString();
    return sdk::gameName("objects", type & ~3, handler.isEmpty() ? key : handler);
}

}  // namespace

LevelsModel::LevelsModel(AlienSoldierSession* session, int stage)
    : TileMapModel(session), session_(session), stage_(stage) {
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Levels || doc == Doc::Maps || doc == Doc::Tiles || doc == Doc::Compressed ||
            doc == Doc::Palettes) {
            emit contentChanged();
        }
    });
    connect(session_, &AlienSoldierSession::playStageChanged, this, &sdk::EditorModel::toolsChanged);
}

QString LevelsModel::title() const { return session_->stageLabel(stage_); }

QJsonObject LevelsModel::record() const {
    const QString label = session_->doc(Doc::Stages)
                              .value(QStringLiteral("scenes"))
                              .toArray()
                              .at(stage_)
                              .toObject()
                              .value(QStringLiteral("record"))
                              .toString();
    const QJsonArray records = session_->doc(Doc::Levels).value(QStringLiteral("records")).toArray();
    const int index = indexOfLabel(records, label);
    return index < 0 ? QJsonObject() : records.at(index).toObject();
}

QString LevelsModel::spawnList() const { return record().value(QStringLiteral("spawn_list")).toString(); }

QJsonObject LevelsModel::placement(int index) const {
    const QJsonArray lists = session_->doc(Doc::Levels).value(QStringLiteral("spawn_lists")).toArray();
    const QJsonObject list = lists.at(indexOfLabel(lists, spawnList())).toObject();
    return list.value(QStringLiteral("placements")).toArray().at(index).toObject();
}

bool LevelsModel::isReadOnly() const { return record().isEmpty(); }

QString LevelsModel::readOnlyReason() const { return tr("This stage slot applies no stage record."); }

QSize LevelsModel::gridSize() const {
    const Scene& scene = session_->stage(stage_);
    QSize extent = scene.extent(0);
    if (extent.isEmpty()) extent = scene.extent(1);
    if (extent.isEmpty()) extent = QSize(320, 224);
    return {extent.width() / 8, extent.height() / 8};
}

QVector<sdk::MapLayer> LevelsModel::layers() const { return {{kObjects, tr("Objects")}, {kChunks, tr("Chunks")}}; }

void LevelsModel::setCurrentLayer(const QString& id) {
    if (id == layer_) return;
    layer_ = id;
    selected_.clear();
    emit toolsChanged();
    emit selectionChanged();
}

QVector<sdk::Tool> LevelsModel::tools() const {
    if (isReadOnly()) return {{QStringLiteral("select"), tr("Select"), QStringLiteral("select")}};
    if (layer_ == kChunks) {
        return {{QStringLiteral("paint"), tr("Paint chunks"), QStringLiteral("pencil")},
                {QStringLiteral("pick"), tr("Pick a chunk"), QStringLiteral("pick")}};
    }
    return {{QStringLiteral("select"), tr("Select and move"), QStringLiteral("select")}};
}

QImage LevelsModel::render() const {
    const Scene& scene = session_->stage(stage_);
    const QSize size = gridSize() * 8;
    QImage image = renderScene(scene, QRect(QPoint(0, 0), size), planeA_, planeB_);
    if (layer_ == kChunks) {
        // The 256-pixel chunk grid.
        QPainter painter(&image);
        painter.setPen(QColor(255, 255, 255, 60));
        for (int x = 256; x < size.width(); x += 256) painter.drawLine(x, 0, x, size.height());
        for (int y = 256; y < size.height(); y += 256) painter.drawLine(0, y, size.width(), y);
    }
    return image;
}

QVector<sdk::ViewOption> LevelsModel::viewOptions() const {
    sdk::ViewOption a;
    a.key = QStringLiteral("plane-a");
    a.label = tr("Plane A");
    a.toggle = true;
    a.value = planeA_ ? QStringLiteral("on") : QStringLiteral("off");
    sdk::ViewOption b;
    b.key = QStringLiteral("plane-b");
    b.label = tr("Plane B");
    b.toggle = true;
    b.value = planeB_ ? QStringLiteral("on") : QStringLiteral("off");
    return {a, b};
}

void LevelsModel::setViewOption(const QString& key, const QString& value) {
    const bool on = value == QLatin1String("on");
    if (key == QLatin1String("plane-a")) planeA_ = on;
    if (key == QLatin1String("plane-b")) planeB_ = on;
    emit toolsChanged();
    emit contentChanged();
}

QString LevelsModel::startPoint() const {
    return stage_ < session_->passwordStages() ? session_->stageLabel(stage_) : QString();
}

QString LevelsModel::statusText() const {
    return selected_.isEmpty() ? session_->stageLabel(stage_)
                               : tr("%1 · object %2").arg(session_->stageLabel(stage_), selected_.mid(1));
}

// ---------------------------------------------------------------- objects

QVector<sdk::MapObject> LevelsModel::objects() const {
    QVector<sdk::MapObject> result;
    if (layer_ != kObjects) return result;
    const QJsonArray lists = session_->doc(Doc::Levels).value(QStringLiteral("spawn_lists")).toArray();
    const int found = indexOfLabel(lists, spawnList());
    if (found < 0) return result;
    const QJsonArray placements = lists.at(found).toObject().value(QStringLiteral("placements")).toArray();
    for (int index = 0; index < placements.size(); ++index) {
        const QJsonObject item = placements.at(index).toObject();
        sdk::MapObject object;
        object.id = QStringLiteral("p%1").arg(index);
        object.label = typeName(session_->doc(Doc::Levels), item.value(QStringLiteral("type")).toInt());
        object.cells = QRect(item.value(QStringLiteral("x")).toInt() / 8 - 1,
                             item.value(QStringLiteral("y")).toInt() / 8 - 1, 2, 2);
        result.append(object);
    }
    return result;
}

void LevelsModel::selectObject(const QString& id) {
    if (id == selected_) return;
    selected_ = id;
    emit selectionChanged();
}

void LevelsModel::beginDrag(const QString& id) {
    selectObject(id);
    strokeBefore_ = session_->doc(Doc::Levels);
    strokeRevision_ = session_->revision(Doc::Levels);
}

bool LevelsModel::dragObject(const QString& id, QPoint topLeftCell) {
    const int index = placementIndex(id);
    if (index < 0) return false;
    const QString list = spawnList();
    QJsonObject& levels = session_->live(Doc::Levels);
    QJsonArray lists = levels.value(QStringLiteral("spawn_lists")).toArray();
    const int at = indexOfLabel(lists, list);
    if (at < 0) return false;
    QJsonObject spawn = lists.at(at).toObject();
    QJsonArray placements = spawn.value(QStringLiteral("placements")).toArray();
    QJsonObject item = placements.at(index).toObject();
    item.insert(QStringLiteral("x"), std::clamp((topLeftCell.x() + 1) * 8, 0, 0xFFFF));
    item.insert(QStringLiteral("y"), std::clamp((topLeftCell.y() + 1) * 8, 0, 0xFFFF));
    placements.replace(index, item);
    spawn.insert(QStringLiteral("placements"), placements);
    lists.replace(at, spawn);
    levels.insert(QStringLiteral("spawn_lists"), lists);
    session_->touch(Doc::Levels);
    return true;
}

void LevelsModel::endDrag() { session_->commit(Doc::Levels, strokeBefore_, strokeRevision_, tr("Move object")); }

// ---------------------------------------------------------------- chunks

int LevelsModel::chunkCount() const { return rl::alien_soldier::chunkCount(session_->stage(stage_), 0); }

QVector<sdk::CellBrush> LevelsModel::brushes() const {
    QVector<sdk::CellBrush> result;
    if (layer_ != kChunks) return result;
    for (int chunk = 0; chunk < chunkCount(); ++chunk)
        result.append({chunk, tr("Chunk %1").arg(hex(chunk, 2)), QColor()});
    return result;
}

void LevelsModel::setCurrentBrush(int value) {
    brush_ = value;
    emit toolsChanged();
}

sdk::Library LevelsModel::sheet() const {
    sdk::Library library;
    if (layer_ != kChunks) return library;
    const Scene& scene = session_->stage(stage_);
    library.hint = tr("pick a chunk, then paint the coarse map");
    library.grid = true;
    library.columns = 4;
    library.cell = QSize(64, 64);
    for (int chunk = 0; chunk < chunkCount(); ++chunk) {
        sdk::LibraryItem item;
        item.id = QString::number(chunk);
        item.label = hex(chunk, 2);
        item.icon = chunkImage(scene, 0, chunk).scaled(64, 64, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        library.items.append(item);
    }
    return library;
}

void LevelsModel::setCurrentLibraryItem(const QString& id) { setCurrentBrush(id.toInt()); }

QString LevelsModel::currentLibraryItem() const { return QString::number(brush_); }

int LevelsModel::coarseValueIndex(QPoint cell, QString* payload) const {
    const Scene& scene = session_->stage(stage_);
    const int offset = scene.coarseOffset(0, cell.x() * 8, cell.y() * 8);
    const MapValue value = mapValueAt(session_->doc(Doc::Maps), session_->doc(Doc::Screens), scene, offset);
    if (payload) *payload = value.payload;
    return value.index;
}

int LevelsModel::cellValue(QPoint cell) const {
    const Scene& scene = session_->stage(stage_);
    const int offset = scene.coarseOffset(0, cell.x() * 8, cell.y() * 8);
    return offset < 0 || offset >= scene.ram.size() ? 0 : quint8(scene.ram.at(offset));
}

bool LevelsModel::isCellEditable(QPoint cell) const { return coarseValueIndex(cell, nullptr) >= 0; }

QString LevelsModel::lockReason(QPoint cell) const {
    QString payload;
    coarseValueIndex(cell, &payload);
    return payload.isEmpty() ? tr("No map is loaded here.") : tr("%1 is not an editable map.").arg(payload);
}

void LevelsModel::beginStroke() {
    strokeBefore_ = session_->doc(Doc::Maps);
    strokeRevision_ = session_->revision(Doc::Maps);
}

bool LevelsModel::paintCell(QPoint cell, int value) {
    const Scene& scene = session_->stage(stage_);
    const int offset = scene.coarseOffset(0, cell.x() * 8, cell.y() * 8);
    const MapValue target = mapValueAt(session_->doc(Doc::Maps), session_->doc(Doc::Screens), scene, offset);
    if (target.index < 0 || target.doc != Doc::Maps || value < 0 || value >= std::max(1, chunkCount())) return false;
    if (cellValue(cell) == value) return true;
    try {
        setMapValue(session_->live(Doc::Maps), target, value);
    } catch (const std::exception&) {
        return false;
    }
    session_->touch(Doc::Maps);
    return true;
}

void LevelsModel::endStroke() { session_->commit(Doc::Maps, strokeBefore_, strokeRevision_, tr("Paint chunks")); }

// ---------------------------------------------------------------- inspector

sdk::Inspection LevelsModel::inspect() const {
    sdk::Inspection inspection;
    const QJsonObject levels = session_->doc(Doc::Levels);
    const int index = placementIndex(selected_);
    if (index >= 0 && layer_ == kObjects) {
        const QJsonObject item = placement(index);
        const int type = item.value(QStringLiteral("type")).toInt();
        inspection.kicker = tr("Object");
        inspection.title = typeName(levels, type);
        for (const QString& key : kPlacementFields) {
            sdk::Field field;
            field.key = key;
            field.label = key == QLatin1String("x")            ? tr("X")
                          : key == QLatin1String("y")          ? tr("Y")
                          : key == QLatin1String("type")       ? tr("Type")
                          : key == QLatin1String("parameter")  ? tr("Parameter")
                          : key == QLatin1String("slot_first") ? tr("First slot")
                                                               : tr("Last slot");
            field.type = sdk::Field::Type::Integer;
            field.value = item.value(key).toInt();
            field.minimum = 0;
            field.maximum = 0xFFFF;
            field.monospace = true;
            if (key == QLatin1String("type")) {
                const QString name = typeName(levels, type);
                field.hint = (type & 2)   ? tr("%1 · easy only").arg(name)
                             : (type & 1) ? tr("%1 · hard only").arg(name)
                                          : name;
            }
            inspection.fields.append(field);
        }
        inspection.note =
            tr("The type picks the object's handler (a multiple of 4); bit 1 keeps it to the easy game, "
               "bit 0 to the hard one. The number of objects is the stage's own.");
        return inspection;
    }
    const QJsonObject stageRecord = record();
    inspection.kicker = tr("Stage");
    inspection.title = session_->stageLabel(stage_);
    for (const QString& key : kRecordFields) {
        sdk::Field field;
        field.key = key;
        field.label = key;
        field.type = sdk::Field::Type::Integer;
        field.value = stageRecord.value(key).toInt();
        field.maximum =
            key.startsWith(QLatin1String("player")) || key.startsWith(QLatin1String("palette_")) ? 0xFF : 0xFFFF;
        field.monospace = true;
        inspection.fields.append(field);
    }
    inspection.engineTag = QStringLiteral("alien-soldier.levels.v1");
    inspection.engine = {{tr("Record"), stageRecord.value(QStringLiteral("label")).toString()},
                         {tr("Objects"), spawnList()},
                         {tr("Palettes"), stageRecord.value(QStringLiteral("palette_list")).toString()}};
    inspection.note =
        tr("The stage record sets where the cameras and the player start and how its planes enter; "
           "the kit refuses values its readers do not index.");
    return inspection;
}

bool LevelsModel::setField(const QString& key, const QVariant& value, QString* error) {
    bool ok = false;
    const int number =
        value.toString().startsWith(QLatin1Char('$')) ? value.toString().mid(1).toInt(&ok, 16) : value.toInt(&ok);
    if (!ok || number < 0 || number > 0xFFFF) {
        if (error) *error = tr("A number from 0 to 65535.");
        return false;
    }
    const int index = placementIndex(selected_);
    if (index >= 0 && kPlacementFields.contains(key)) {
        const QString list = spawnList();
        return session_->edit(
            Doc::Levels, tr("Change object"),
            [&](QJsonObject& levels) {
                QJsonArray lists = levels.value(QStringLiteral("spawn_lists")).toArray();
                const int at = indexOfLabel(lists, list);
                QJsonObject spawn = lists.at(at).toObject();
                QJsonArray placements = spawn.value(QStringLiteral("placements")).toArray();
                QJsonObject item = placements.at(index).toObject();
                item.insert(key, number);
                placements.replace(index, item);
                spawn.insert(QStringLiteral("placements"), placements);
                lists.replace(at, spawn);
                levels.insert(QStringLiteral("spawn_lists"), lists);
            },
            error);
    }
    if (!kRecordFields.contains(key)) return false;
    const QString label = record().value(QStringLiteral("label")).toString();
    return session_->edit(
        Doc::Levels, tr("Change stage"),
        [&](QJsonObject& levels) {
            QJsonArray records = levels.value(QStringLiteral("records")).toArray();
            const int at = indexOfLabel(records, label);
            QJsonObject item = records.at(at).toObject();
            item.insert(key, number);
            records.replace(at, item);
            levels.insert(QStringLiteral("records"), records);
        },
        error);
}

}  // namespace rl::alien_soldier
