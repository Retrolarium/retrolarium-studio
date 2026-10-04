#include "castle/plugin/room_model.hpp"

#include "castle/plugin/castle_session.hpp"
#include "nes/editors.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace rl::castle {

namespace {

constexpr int kObjectStreamBytes = 2449;     // $EF6B-$F8FB, three bytes per object
constexpr int kStateBytes = 141;             // one persistent bit per object, per room rounded to bytes

QString hex(int value) {
    return nes::hex(value);
}

sdk::Field integer(const QString& key, const QString& label, int value, int minimum, int maximum,
                   const QString& hint = QString()) {
    sdk::Field field;
    field.key = key;
    field.label = label;
    field.type = sdk::Field::Type::Integer;
    field.value = value;
    field.minimum = minimum;
    field.maximum = maximum;
    field.hint = hint.isEmpty() ? QStringLiteral("%1-%2").arg(minimum).arg(maximum) : hint;
    return field;
}

sdk::Field readOnly(const QString& key, const QString& label, const QVariant& value) {
    sdk::Field field;
    field.key = key;
    field.label = label;
    field.value = value;
    field.monospace = true;
    return field;
}


}  // namespace

CastleRoomModel::CastleRoomModel(CastleSession* session, int room)
    : TileMapModel(session), session_(session), room_(room) {
    connect(session_, &sdk::Session::contentChanged, this, [this] {
        if (!selected_.isEmpty() && selected_ != QLatin1String("start") && objectIndex(selected_) < 0) {
            selected_.clear();
        }
        emit contentChanged();
    });
    connect(session_, &CastleSession::startChanged, this, [this] {
        emit contentChanged();
        emit toolsChanged();
    });
    // The room being edited is where Play from selected starts.
    if (session_->start().room != room_ + 1) {
        CastleStart start = session_->start();
        start.room = room_ + 1;
        session_->setStart(start);
    }
}

QString CastleRoomModel::title() const {
    return roomLabel(session_->levels().roomNumber(room_));
}

QSize CastleRoomModel::gridSize() const {
    return {session_->levels().width(), session_->levels().height()};
}

QVector<sdk::MapLayer> CastleRoomModel::layers() const {
    return {{QStringLiteral("cells"), QStringLiteral("Cells")}, {QStringLiteral("objects"), QStringLiteral("Objects")}};
}

void CastleRoomModel::setCurrentLayer(const QString& id) {
    if (id != layer_ && (id == QLatin1String("cells") || id == QLatin1String("objects"))) {
        layer_ = id;
        selected_.clear();
        emit toolsChanged();
        emit selectionChanged();
        emit contentChanged();
    }
}

QVector<sdk::Tool> CastleRoomModel::tools() const {
    if (layer_ == QLatin1String("cells")) {
        return {{QStringLiteral("paint"), QStringLiteral("Paint the sheet's cell (right button takes one)"),
                 QStringLiteral("paint")},
                {QStringLiteral("pick"), QStringLiteral("Take a cell's type as the brush"), QStringLiteral("pick")}};
    }
    return {{QStringLiteral("select"), QStringLiteral("Select and move objects and the start point"),
             QStringLiteral("select")},
            {QStringLiteral("place"), QStringLiteral("Add the library object"), QStringLiteral("place")},
            {QStringLiteral("remove"), QStringLiteral("Delete objects"), QStringLiteral("erase")}};
}

QVector<int> CastleRoomModel::roomCells() const {
    QVector<int> cells = session_->levels().cells(room_);
    for (auto it = pending_.constBegin(); it != pending_.constEnd(); ++it) {
        cells[it.key()] = it.value();
    }
    return cells;
}

QVector<CastleObject> CastleRoomModel::roomObjects() const {
    return dragged_ ? *dragged_ : session_->levels().objects(room_);
}

QRect CastleRoomModel::startCells() const {
    const QPoint pixel = session_->start().pixel;
    return QRect(pixel.x() / 16, pixel.y() / 8, 1, 2);
}

QImage CastleRoomModel::render() const {
    QImage image = session_->painter().room(roomCells(), roomObjects());
    if (session_->start().room == room_ + 1) {
        QPainter painter(&image);
        painter.drawImage(session_->start().pixel, session_->painter().player());
    }
    return image;
}

// ---------------------------------------------------------------- cells

QVector<sdk::CellBrush> CastleRoomModel::brushes() const {
    if (layer_ != QLatin1String("cells")) {
        return {};
    }
    return {{brush_, QStringLiteral("Cell %1").arg(hex(brush_)), QColor()}};
}

void CastleRoomModel::setCurrentBrush(int value) {
    brush_ = std::clamp(value, 0, std::max(0, session_->painter().cellTypeCount() - 1));
    emit toolsChanged();
    emit selectionChanged();
}

QString CastleRoomModel::currentLibraryItem() const {
    return layer_ == QLatin1String("cells") ? QStringLiteral("cell:%1").arg(brush_)
                                            : QStringLiteral("object:%1").arg(objectType_);
}

void CastleRoomModel::setCurrentLibraryItem(const QString& id) {
    if (id.startsWith(QLatin1String("cell:"))) {
        setCurrentBrush(id.mid(5).toInt());
    } else if (id.startsWith(QLatin1String("object:"))) {
        objectType_ = std::clamp(id.mid(7).toInt(), 0, std::max(0, session_->levels().encodedTypeCount() - 1));
        emit selectionChanged();
    }
}

sdk::Library CastleRoomModel::sheet() const {
    sdk::Library library;
    if (layer_ != QLatin1String("cells")) {
        return library;
    }
    if (sheetRevision_ != session_->revision()) {
        cellSheet_.clear();
        objectSheet_.clear();
        sheetRevision_ = session_->revision();
    }
    if (cellSheet_.isEmpty()) {
        for (int type = 0; type < session_->painter().cellTypeCount(); ++type) {
            sdk::LibraryItem item;
            item.id = QStringLiteral("cell:%1").arg(type);
            item.label = QStringLiteral("Cell %1").arg(hex(type));
            // Square, as the sheet draws: the 16x8 cell over a second copy.
            QImage icon(16, 16, QImage::Format_RGB32);
            const QImage cell = session_->painter().cellIcon(type);
            QPainter p(&icon);
            p.drawImage(0, 0, cell);
            p.drawImage(0, 8, cell);
            item.icon = icon;
            cellSheet_ << item;
        }
    }
    library.grid = true;
    library.hint = QStringLiteral("click: brush · drag onto a cell");
    library.items = cellSheet_;
    return library;
}

int CastleRoomModel::cellValue(QPoint cell) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= gridSize().width() || cell.y() >= gridSize().height()) {
        return -1;
    }
    return roomCells().value(cell.y() * gridSize().width() + cell.x(), -1);
}

bool CastleRoomModel::isCellEditable(QPoint cell) const {
    return layer_ == QLatin1String("cells") && cellValue(cell) >= 0;
}

void CastleRoomModel::beginStroke() {
    stroking_ = true;
    pending_.clear();
}

bool CastleRoomModel::paintCell(QPoint cell, int value) {
    if (!isCellEditable(cell) || value < 0 || value >= session_->painter().cellTypeCount()) {
        return false;
    }
    const int index = cell.y() * gridSize().width() + cell.x();
    if (roomCells().value(index) == value) {
        return false;
    }
    if (!stroking_) {
        const int room = room_;
        return session_->edit(QStringLiteral("Paint %1").arg(title()),
                              [room, cell, value](CastleState& s) { s.levels.setCell(room, cell, value); }, nullptr);
    }
    pending_.insert(index, value);
    emit contentChanged();
    return true;
}

void CastleRoomModel::endStroke() {
    if (!stroking_) {
        return;
    }
    stroking_ = false;
    const QHash<int, int> cells = pending_;
    pending_.clear();
    if (!cells.isEmpty()) {
        const int room = room_;
        const int width = gridSize().width();
        session_->edit(QStringLiteral("Paint %1").arg(title()),
                       [room, cells, width](CastleState& s) {
                           for (auto it = cells.constBegin(); it != cells.constEnd(); ++it) {
                               s.levels.setCell(room, QPoint(it.key() % width, it.key() / width), it.value());
                           }
                       },
                       nullptr);
    }
    emit contentChanged();
}

// ---------------------------------------------------------------- objects

QString CastleRoomModel::typeLabel(int type) const {
    const int runtime = session_->painter().runtimeType(type);
    return runtime < 0 ? QStringLiteral("Object %1").arg(hex(type))
                       : QStringLiteral("Object %1 · runtime %2").arg(hex(type), hex(runtime));
}

int CastleRoomModel::objectIndex(const QString& id) const {
    if (!id.startsWith(QLatin1String("object:"))) {
        return -1;
    }
    const int index = id.mid(7).toInt();
    return index >= 0 && index < roomObjects().size() ? index : -1;
}

QVector<sdk::MapObject> CastleRoomModel::objects() const {
    QVector<sdk::MapObject> result;
    if (layer_ != QLatin1String("objects")) {
        return result;
    }
    const QVector<CastleObject> list = roomObjects();
    for (int i = 0; i < list.size(); ++i) {
        const QRect pixels = session_->painter().bounds(list.at(i));
        sdk::MapObject o;
        o.id = QStringLiteral("object:%1").arg(i);
        o.label = typeLabel(list.at(i).type);
        o.cells = QRect(QPoint(pixels.left() / 16, pixels.top() / 8),
                        QPoint(std::max(pixels.left() / 16, (pixels.right()) / 16), pixels.bottom() / 8));
        result << o;
    }
    if (session_->start().room == room_ + 1) {
        sdk::MapObject start;
        start.id = QStringLiteral("start");
        start.label = QStringLiteral("Start point");
        start.cells = startCells();
        result << start;
    }
    return result;
}

void CastleRoomModel::selectObject(const QString& id) {
    if (id != selected_) {
        selected_ = id;
        emit selectionChanged();
    }
}

void CastleRoomModel::beginDrag(const QString& id) {
    selectObject(id);
    if (id == QLatin1String("start")) {
        const QPoint pixel = session_->start().pixel;
        dragStartOffset_ = QPoint(pixel.x() % 16, pixel.y() % 8);
        return;
    }
    if (objectIndex(id) >= 0) {
        dragged_ = session_->levels().objects(room_);
    }
}

bool CastleRoomModel::dragObject(const QString& id, QPoint topLeftCell) {
    const QSize grid = gridSize();
    if (id == QLatin1String("start")) {
        CastleStart start = session_->start();
        start.pixel = QPoint(std::clamp(topLeftCell.x(), 0, grid.width() - 1) * 16 + dragStartOffset_.x(),
                             std::clamp(topLeftCell.y(), 0, grid.height() - 2) * 8 + dragStartOffset_.y());
        if (start.pixel == session_->start().pixel) {
            return false;
        }
        session_->setStart(start);
        return true;
    }
    const int index = objectIndex(id);
    if (!dragged_ || index < 0) {
        return false;
    }
    const QPoint to(std::clamp(topLeftCell.x(), 0, grid.width() - 1), std::clamp(topLeftCell.y(), 0, grid.height() - 1));
    CastleObject& object = (*dragged_)[index];
    if (object.x == to.x() && object.y == to.y()) {
        return false;
    }
    object.x = to.x();
    object.y = to.y();
    emit contentChanged();
    return true;
}

void CastleRoomModel::endDrag() {
    if (!dragged_) {
        return;
    }
    const QVector<CastleObject> moved = *dragged_;
    dragged_.reset();
    if (moved != session_->levels().objects(room_)) {
        commitObjects(moved, QStringLiteral("Move an object in %1").arg(title()), nullptr);
    }
    emit contentChanged();
}

bool CastleRoomModel::commitObjects(const QVector<CastleObject>& objects, const QString& text, QString* error) {
    if (objects.size() > kMaximumObjects) {
        if (error) {
            *error = QStringLiteral("A room holds at most %1 objects; the game keeps them in a fixed pool.")
                         .arg(kMaximumObjects);
        }
        return false;
    }
    const int room = room_;
    return session_->edit(text, [room, objects](CastleState& s) { s.levels.setObjects(room, objects); }, error);
}

bool CastleRoomModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    if (libraryItem.startsWith(QLatin1String("cell:"))) {
        // A cell dropped from the sheet paints the cell under it.
        setCurrentLibraryItem(libraryItem);
        if (layer_ != QLatin1String("cells")) {
            setCurrentLayer(QStringLiteral("cells"));
        }
        if (!paintCell(cell, brush_) && cellValue(cell) != brush_) {
            if (error) *error = QStringLiteral("Drop a cell inside the room.");
            return false;
        }
        return true;
    }
    if (!libraryItem.startsWith(QLatin1String("object:"))) {
        if (error) *error = QStringLiteral("Pick an object in the library first.");
        return false;
    }
    const int type = libraryItem.mid(7).toInt();
    QVector<CastleObject> list = session_->levels().objects(room_);
    list << CastleObject{type, std::clamp(cell.x(), 0, gridSize().width() - 1),
                         std::clamp(cell.y(), 0, gridSize().height() - 1)};
    if (!commitObjects(list, QStringLiteral("Add %1 to %2").arg(typeLabel(type), title()), error)) {
        return false;
    }
    selectObject(QStringLiteral("object:%1").arg(list.size() - 1));
    return true;
}

bool CastleRoomModel::removeObject(const QString& id, QString* error) {
    const int index = objectIndex(id);
    if (index < 0) {
        if (id == QLatin1String("start") && error) {
            *error = QStringLiteral("The start point stays; drag it where the player should begin.");
        }
        return false;
    }
    QVector<CastleObject> list = session_->levels().objects(room_);
    const QString label = typeLabel(list.at(index).type);
    list.removeAt(index);
    selectObject(QString());
    return commitObjects(list, QStringLiteral("Delete %1 from %2").arg(label, title()), error);
}

sdk::Library CastleRoomModel::objectLibrary() const {
    sdk::Library library;
    if (sheetRevision_ != session_->revision()) {
        cellSheet_.clear();
        objectSheet_.clear();
        sheetRevision_ = session_->revision();
    }
    if (objectSheet_.isEmpty()) {
        for (int type = 0; type < session_->levels().encodedTypeCount(); ++type) {
            sdk::LibraryItem item;
            item.id = QStringLiteral("object:%1").arg(type);
            item.label = QStringLiteral("Object %1").arg(hex(type));
            const int runtime = session_->painter().runtimeType(type);
            item.hint = runtime < 0 ? QString() : QStringLiteral("runtime %1").arg(hex(runtime));
            item.icon = session_->painter().objectIcon(type);
            objectSheet_ << item;
        }
    }
    library.hint = QStringLiteral("drag onto the room, or pick and use the Add tool");
    library.items = objectSheet_;
    return library;
}

// ---------------------------------------------------------------- inspector

sdk::Budget CastleRoomModel::budget() const {
    const CastleLevels& levels = session_->levels();
    int total = 0;
    int state = 0;
    for (int r = 0; r < levels.roomCount(); ++r) {
        const int count = r == room_ ? roomObjects().size() : levels.objects(r).size();
        total += count;
        state += (count + 7) / 8;
    }
    sdk::Budget budget;
    budget.used = roomObjects().size();
    budget.capacity = kMaximumObjects;
    budget.text = QStringLiteral("this room %1 / %2 objects · all rooms: stream %3 / %4 B, saved state %5 / %6 B")
                      .arg(roomObjects().size())
                      .arg(kMaximumObjects)
                      .arg(total * 3)
                      .arg(kObjectStreamBytes)
                      .arg(state)
                      .arg(kStateBytes);
    return budget;
}

QString CastleRoomModel::startPoint() const {
    return session_->start().room == room_ + 1 ? session_->selectedStart() : QString();
}

QString CastleRoomModel::statusText() const {
    return QStringLiteral("%1 · %2 objects · %3")
        .arg(title())
        .arg(roomObjects().size())
        .arg(layer_ == QLatin1String("cells") ? QStringLiteral("brush %1").arg(hex(brush_)) : QStringLiteral("objects"));
}

sdk::Inspection CastleRoomModel::inspect() const {
    sdk::Inspection inspection;
    const int index = objectIndex(selected_);
    if (index >= 0) {
        const CastleObject object = roomObjects().at(index);
        inspection.kicker = QStringLiteral("Room object");
        inspection.title = typeLabel(object.type);
        sdk::Field type;
        type.key = QStringLiteral("type");
        type.label = QStringLiteral("Type");
        type.type = sdk::Field::Type::Choice;
        type.value = QStringLiteral("Object %1").arg(hex(object.type));
        const sdk::Library library = objectLibrary();
        for (const sdk::LibraryItem& item : library.items) {
            type.choices << item.label;
            type.choiceIcons << item.icon;
        }
        inspection.fields = {type, integer(QStringLiteral("x"), QStringLiteral("Column"), object.x, 0, gridSize().width() - 1,
                                           QStringLiteral("16 px")),
                             integer(QStringLiteral("y"), QStringLiteral("Row"), object.y, 0, gridSize().height() - 1,
                                     QStringLiteral("8 px"))};
        inspection.note = QStringLiteral("Drag to move it, Delete to remove it. The game keeps one bit per object "
                                         "in the saved game, so a collected key stays collected.");
        inspection.engineTag = QStringLiteral("castle.object-stream.v1");
        inspection.engine = {{QStringLiteral("Encoded type"), hex(object.type)},
                             {QStringLiteral("Runtime type"), hex(session_->painter().runtimeType(object.type))},
                             {QStringLiteral("Record"), QStringLiteral("%1 %2 %3").arg(hex(object.type), hex(object.x), hex(object.y))}};
        inspection.library = objectLibrary();
        return inspection;
    }
    if (selected_ == QLatin1String("start")) {
        const CastleStart start = session_->start();
        inspection.kicker = QStringLiteral("Start point");
        inspection.title = session_->selectedStart();
        inspection.fields = {integer(QStringLiteral("start_x"), QStringLiteral("X"), start.pixel.x(), 0, 319,
                                     QStringLiteral("px")),
                             integer(QStringLiteral("start_y"), QStringLiteral("Y"), start.pixel.y(), 0, 159,
                                     QStringLiteral("px"))};
        inspection.note = QStringLiteral("Play from selected (F5) builds the ROM, enters this room through the "
                                         "game's own room loader and puts the player here.");
        inspection.engineTag = QStringLiteral("castle-point-playtest");
        inspection.library = objectLibrary();
        return inspection;
    }
    inspection.kicker = QStringLiteral("Room");
    inspection.title = title();
    const QVector<int> cells = roomCells();
    inspection.fields = {readOnly(QStringLiteral("objects"), QStringLiteral("Objects"),
                                  QStringLiteral("%1 / %2").arg(roomObjects().size()).arg(kMaximumObjects)),
                         readOnly(QStringLiteral("brush"), QStringLiteral("Cell brush"), hex(brush_))};
    inspection.note = layer_ == QLatin1String("cells")
                          ? QStringLiteral("Paint cells with the sheet's pattern; the right button takes a cell. "
                                           "Each cell is two background tiles; its colours come from the attribute "
                                           "bytes the room loader writes, so a cell may recolour its neighbours.")
                          : QStringLiteral("Drag objects from the library onto the room. Drag the start marker "
                                           "where Play from selected should put the player.");
    inspection.engineTag = QStringLiteral("castle.prefix-rooms@1");
    inspection.engine = {{QStringLiteral("Room number"), QString::number(session_->levels().roomNumber(room_))},
                         {QStringLiteral("Cells"), QStringLiteral("20x20, 16x8 px each")}};
    if (layer_ == QLatin1String("objects")) {
        inspection.library = objectLibrary();
    }
    return inspection;
}

bool CastleRoomModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("start_x") || key == QLatin1String("start_y")) {
        CastleStart start = session_->start();
        if (key == QLatin1String("start_x")) start.pixel.setX(value.toInt());
        else start.pixel.setY(value.toInt());
        session_->setStart(start);
        return true;
    }
    const int index = objectIndex(selected_);
    if (index < 0) {
        return TileMapModel::setField(key, value, error);
    }
    QVector<CastleObject> list = session_->levels().objects(room_);
    CastleObject& object = list[index];
    if (key == QLatin1String("type")) {
        const QString text = value.toString();
        bool ok = false;
        const int type = text.section(QLatin1Char('$'), 1).left(2).toInt(&ok, 16);
        if (!ok) {
            if (error) *error = QStringLiteral("Pick a type from the list.");
            return false;
        }
        object.type = type;
    } else if (key == QLatin1String("x")) {
        object.x = value.toInt();
    } else if (key == QLatin1String("y")) {
        object.y = value.toInt();
    } else {
        return TileMapModel::setField(key, value, error);
    }
    return commitObjects(list, QStringLiteral("Edit %1 in %2").arg(typeLabel(object.type), title()), error);
}

}  // namespace rl::castle
