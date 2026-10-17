#include "smb/plugin/level_model.hpp"

#include "smb/data/area_streams.hpp"
#include "smb/data/catalog.hpp"
#include "smb/plugin/smb_session.hpp"

#include <QPainter>

#include <algorithm>

namespace rl::smb {

namespace {

constexpr int kLastPage = 31;

QString hex(int value) {
    return QStringLiteral("$%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

// Cells a pixel rectangle covers, kept inside the play field.
QRect cellsOf(const QRect& pixels) {
    const int left = std::max(0, pixels.left()) / 16;
    const int top = std::max(0, pixels.top()) / 16;
    const int right = std::max(0, pixels.right()) / 16;
    const int bottom = std::min(12, std::max(0, pixels.bottom()) / 16);
    return QRect(QPoint(left, top), QPoint(std::max(left, right), std::max(top, bottom)));
}

sdk::Field readOnly(const QString& key, const QString& label, const QVariant& value, bool mono = false) {
    sdk::Field field;
    field.key = key;
    field.label = label;
    field.value = value;
    field.monospace = mono;
    return field;
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

sdk::Field choice(const QString& key, const QString& label, const QString& value, const QStringList& choices) {
    sdk::Field field;
    field.key = key;
    field.label = label;
    field.type = sdk::Field::Type::Choice;
    field.value = value;
    field.choices = choices;
    return field;
}

const QStringList& timers() {
    static const QStringList kTimers{QStringLiteral("none"), QStringLiteral("400"), QStringLiteral("300"),
                                     QStringLiteral("200")};
    return kTimers;
}

const QStringList& yesNo() {
    static const QStringList kChoices{QStringLiteral("no"), QStringLiteral("yes")};
    return kChoices;
}

// The catalog class an object belongs to: the catalog row its kind lists
// under (8 for rows 0-11, 12, 13 for the row-13 specials), or -1 when the
// record is a command the editor does not offer (row 14 and 15, page jumps).
int objectClass(const SmbAreaObject& object) {
    if (object.row <= 11) return 8;
    if (object.row == 12) return 12;
    if (object.row == 13 && (object.control & 0x40)) return 13;
    return -1;
}

// Objects with a size: rows, columns, pipes, platforms, and every row-12
// object. The low nibble holds the size minus one.
bool hasLength(const SmbAreaObject& object) {
    if (object.row <= 11) return ((object.control >> 4) & 7) != 0;
    return object.row == 12;
}

// The bits of the low nibble that hold the size: a pipe's top bit says
// whether it can be entered, so its size is three bits.
int lengthMask(const SmbAreaObject& object) {
    return object.row <= 11 && ((object.control >> 4) & 7) == 7 ? 7 : 15;
}

// A new object of a kind: rows, columns and other sized objects start three
// cells long.
SmbAreaObject newObject(const SmbObjectKind& kind, int column, int row) {
    SmbAreaObject object{column, kind.row == 8 ? row : kind.row, false, kind.control};
    if (hasLength(object)) {
        object.control |= 2;
    }
    return object;
}

// The pixels of `image` that differ from `blank`, on transparency and cut
// to their bounds; null when nothing shows (hidden blocks, scroll stops).
QImage difference(const QImage& image, const QImage& blank) {
    QImage result = image.convertToFormat(QImage::Format_ARGB32);
    const QImage base = blank.convertToFormat(QImage::Format_ARGB32);
    QRect bounds;
    for (int y = 0; y < result.height() && y < base.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
        const auto* under = reinterpret_cast<const QRgb*>(base.constScanLine(y));
        for (int x = 0; x < result.width() && x < base.width(); ++x) {
            if (line[x] == under[x]) {
                line[x] = 0;
            } else {
                bounds |= QRect(x, y, 1, 1);
            }
        }
    }
    return bounds.isEmpty() ? QImage() : result.copy(bounds);
}

int recordIndex(const QString& id, const char* prefix) {
    const QString start = QString::fromLatin1(prefix);
    return id.startsWith(start) ? id.mid(start.size()).toInt() : -1;
}

}  // namespace

SmbLevelModel::SmbLevelModel(SmbSession* session, const QString& area)
    : TileMapModel(session), session_(session), area_(area) {
    connect(session_, &SmbSession::documentChanged, this, &SmbLevelModel::checkSelection);
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &SmbSession::startChanged, this, [this] {
        emit contentChanged();
        emit toolsChanged();
    });
}

SmbArea SmbLevelModel::area() const {
    if (dragArea_) {
        return *dragArea_;
    }
    return session_->document().area(session_->levels(), area_);
}

SmbLevelPool SmbLevelModel::pool(const SmbArea* edited) const {
    if (!dragOriginal_) {
        return session_->document().levelPool(session_->levels(), edited);
    }
    if (dragAreas_.isEmpty()) {
        return {};
    }
    const SmbArea& current = edited ? *edited : dragArea_ ? *dragArea_ : *dragOriginal_;
    QVector<SmbArea> areas = dragAreas_;
    for (SmbArea& a : areas) {
        if (a.name == current.name) {
            a = current;
        }
    }
    return SmbResourceDocument::poolOf(areas);
}

const SmbLevelScene& SmbLevelModel::scene() const {
    if (sceneRevision_ != session_->revision() || sceneSerial_ != dragSerial_) {
        const SmbArea a = area();
        const QPair<int, int> course = session_->courseOf(area_);
        scene_ = renderSmbLevelScene(area_, a.header, a.objects, a.enemies, std::max(0, course.first - 1),
                                     session_->levelRendering());
        sceneRevision_ = session_->revision();
        sceneSerial_ = dragSerial_;
    }
    return scene_;
}

QString SmbLevelModel::title() const {
    QString type = areaTypeOf(area_);
    if (!type.isEmpty()) {
        type[0] = type.at(0).toUpper();
    }
    return QStringLiteral("%1 %2").arg(type, area_.section(QLatin1Char('_'), -1));
}

QSize SmbLevelModel::gridSize() const {
    return {std::max(16, scene().width), SmbLevelPainter::kRows};
}

QVector<sdk::Tool> SmbLevelModel::tools() const {
    return {{QStringLiteral("select"), tr("Select and move (the player: the start point)"),
             QStringLiteral("select")},
            {QStringLiteral("place"), tr("Add the library item"), QStringLiteral("place")},
            {QStringLiteral("remove"), tr("Delete objects and enemies"), QStringLiteral("erase")}};
}

QImage SmbLevelModel::render() const {
    const QString type = session_->paletteKey(area_);
    QImage image = session_->painter().render(scene(), type, session_->night(area_));
    const QImage player = session_->painter().player();
    if (!player.isNull()) {
        QPainter painter(&image);
        painter.drawImage(session_->painter().playerBounds(session_->playerCell(area_)).topLeft(), player);
    }
    return image;
}

QVector<sdk::MapObject> SmbLevelModel::objects() const {
    QVector<sdk::MapObject> result;
    const QString type = session_->paletteKey(area_);
    for (const SmbPositionedObject& object : scene().objects) {
        if (isPageCommand(object.record)) {
            continue;
        }
        sdk::MapObject o;
        o.id = QStringLiteral("object:%1").arg(object.index);
        o.label = areaObjectName(object.record.row, object.record.control);
        o.cells = QRect(object.x, std::min(object.record.row, 12), std::max(1, object.width), 1);
        o.movable = true;
        o.outlined = false;
        result << o;
    }
    for (const SmbPositionedEnemy& enemy : scene().enemies) {
        if (enemy.record.kind != QLatin1String("enemy")) {
            continue;  // area entrances are route data
        }
        sdk::MapObject o;
        o.id = QStringLiteral("enemy:%1").arg(enemy.index);
        o.label = enemyName(enemy.record.objectOrPage);
        o.cells = cellsOf(session_->painter().enemyBounds(enemy, type));
        o.movable = true;
        result << o;
    }
    sdk::MapObject player;
    player.id = QStringLiteral("player");
    player.label = tr("Player · start point");
    player.cells = cellsOf(session_->painter().playerBounds(session_->playerCell(area_)));
    player.movable = true;
    result << player;
    return result;
}

void SmbLevelModel::selectObject(const QString& id) {
    const bool changed = id != selected_;
    selected_ = id;
    selectedRecord_ = selectedRecord();
    if (changed) {
        emit selectionChanged();
    }
}

QVariantList SmbLevelModel::selectedRecord() const {
    const SmbArea a = area();
    const int o = recordIndex(selected_, "object:");
    const int e = recordIndex(selected_, "enemy:");
    if (o >= 0 && o < a.objects.size()) {
        const SmbAreaObject& r = a.objects.at(o);
        return {QStringLiteral("object"), r.column, r.row, r.pageAdvance, r.control};
    }
    if (e >= 0 && e < a.enemies.size()) {
        const SmbEnemyRecord& r = a.enemies.at(e);
        return {r.kind, r.column, r.row, r.pageAdvance, r.hardMode, r.objectOrPage};
    }
    return {};
}

void SmbLevelModel::checkSelection() {
    if (editing_ || !dragging_.isEmpty() || selected_.isEmpty() || selected_ == QLatin1String("player")) {
        return;
    }
    if (selectedRecord() != selectedRecord_) {
        selectObject(QString());
    }
}

void SmbLevelModel::setCurrentLibraryItem(const QString& id) {
    if (id != libraryItem_) {
        libraryItem_ = id;
        emit selectionChanged();
    }
}

// ---------------------------------------------------------------- editing

bool SmbLevelModel::roomFor(const SmbArea& edited, QString* error) const {
    const SmbLevelPool pool = this->pool(&edited);
    if (pool.pooled) {
        if (pool.objectUsed > pool.objectSize) {
            if (error) {
                *error = tr("All levels share %1 bytes for objects; this would need %2. Delete or "
                            "shorten an object in any level first.")
                             .arg(pool.objectSize)
                             .arg(pool.objectUsed);
            }
            return false;
        }
        if (pool.enemyUsed > pool.enemySize) {
            if (error) {
                *error = tr("All levels share %1 bytes for enemies; this would need %2. Delete an "
                            "enemy in any level first.")
                             .arg(pool.enemySize)
                             .arg(pool.enemyUsed);
            }
            return false;
        }
        if (edited.areaBytes() > 256 || edited.enemyBytes() > 256) {
            if (error) {
                *error = tr("One level holds at most 256 bytes of objects and of enemies; the game "
                            "reads them with an 8-bit index.");
            }
            return false;
        }
        return true;
    }
    if (!edited.areaFits()) {
        if (error) {
            *error = tr("%1 has no room: its objects would take %2 of the %3 bytes the ROM keeps "
                        "for them. Each area has a fixed space; delete or shorten an object first.")
                         .arg(title())
                         .arg(edited.areaBytes())
                         .arg(edited.areaCapacity);
        }
        return false;
    }
    if (!edited.enemiesFit()) {
        if (error) {
            *error = tr("%1 has no room: its enemies would take %2 of the %3 bytes the ROM keeps "
                        "for them. Each area has a fixed space; delete an enemy first.")
                         .arg(title())
                         .arg(edited.enemyBytes())
                         .arg(edited.enemyCapacity);
        }
        return false;
    }
    return true;
}

bool SmbLevelModel::commitArea(const SmbArea& requested, const QString& text, QString* error) {
    SmbArea edited = requested;
    if (!pool(&edited).pooled) {
        // A stream that fills its space exactly ends on the next one's first
        // byte (an implicit terminator). Shorter, it needs its own terminator,
        // which then fits; grown back to the full space, it shares again as
        // the saved ROM did.
        const SmbResourceDocument& saved = session_->saved();
        const bool known = saved.areaNames(session_->levels()).contains(edited.name);
        const SmbArea original = known ? saved.area(session_->levels(), edited.name) : SmbArea{};
        if (edited.areaImplicitTerminator && edited.areaBytes() <= edited.areaCapacity) {
            edited.areaImplicitTerminator = false;
        } else if (!edited.areaImplicitTerminator && original.areaImplicitTerminator &&
                   edited.areaBytes() == edited.areaCapacity + 1) {
            edited.areaImplicitTerminator = true;
        }
        if (edited.enemyImplicitTerminator && edited.enemyBytes() <= edited.enemyCapacity) {
            edited.enemyImplicitTerminator = false;
        } else if (!edited.enemyImplicitTerminator && original.enemyImplicitTerminator &&
                   edited.enemyBytes() == edited.enemyCapacity + 1) {
            edited.enemyImplicitTerminator = true;
        }
    }
    if (!roomFor(edited, error)) {
        return false;
    }
    const QString levels = session_->levels();
    editing_ = true;
    const bool ok =
        session_->edit(text, [levels, edited](SmbResourceDocument& d) { d.setArea(levels, edited); }, error);
    editing_ = false;
    selectedRecord_ = selectedRecord();
    return ok;
}

std::optional<SmbArea> SmbLevelModel::moved(const SmbArea& original, const QString& id, int column, int row,
                                            int* index) const {
    SmbArea next = original;
    const int page = std::clamp(column / 16, 0, kLastPage);
    column = std::clamp(column, 0, kLastPage * 16 + 15) % 16;
    const int o = recordIndex(id, "object:");
    const int e = recordIndex(id, "enemy:");
    if (o >= 0 && o < next.objects.size()) {
        SmbAreaObject record = next.objects.at(o);
        if (record.row <= 11) {
            record.row = std::clamp(row, 0, 11);  // rows 12 and up are object classes, not heights
        }
        if (objectPages(next.objects).at(o) == page && record.column == column) {
            next.objects[o] = record;  // same place in the stream: only the row changes
            *index = o;
        } else {
            record.column = column;
            smb::removeObject(next.objects, o);
            *index = insertObject(next.objects, page, record);
        }
        return next;
    }
    if (e >= 0 && e < next.enemies.size()) {
        SmbEnemyRecord record = next.enemies.at(e);
        record.row = std::clamp(row, 0, 13);
        if (enemyPages(next.enemies).at(e) == page && record.column == column) {
            next.enemies[e] = record;
            *index = e;
        } else {
            record.column = column;
            removeEnemy(next.enemies, e);
            *index = insertEnemy(next.enemies, page, record);
        }
        return next;
    }
    return std::nullopt;
}

void SmbLevelModel::beginDrag(const QString& id) {
    selectObject(id);
    dragging_ = id;
    dragCell_ = QPoint(-1, -1);
    if (id == QLatin1String("player")) {
        const QPoint feet = session_->playerCell(area_);
        dragOffset_ = feet - cellsOf(session_->painter().playerBounds(feet)).topLeft();
        return;
    }
    const SmbResourceDocument& d = session_->document();
    dragOriginal_ = area();
    dragAreas_ = d.levelsPooled(session_->levels()) ? d.areas(session_->levels()) : QVector<SmbArea>();
}

bool SmbLevelModel::dragObject(const QString& id, QPoint topLeftCell) {
    if (id == QLatin1String("player")) {
        // The start point is not level data: it moves without an undo step.
        const QPoint feet(std::clamp(topLeftCell.x() + dragOffset_.x(), 0, std::max(0, scene().width - 1)),
                          std::clamp(topLeftCell.y() + dragOffset_.y(), 1, 12));
        if (feet == session_->playerCell(area_)) {
            return false;
        }
        session_->setPlayerCell(area_, feet);
        return true;
    }
    if (!dragOriginal_ || id != dragging_ || topLeftCell == dragCell_) {
        return false;
    }
    dragCell_ = topLeftCell;
    // From the state the drag started in, so the record moves as one piece.
    const SmbArea& original = *dragOriginal_;
    const bool enemy = id.startsWith(QLatin1String("enemy:"));
    int index = -1;
    // An enemy record's row is two below the cell its sprite starts in.
    const std::optional<SmbArea> next =
        moved(original, id, topLeftCell.x(), topLeftCell.y() + (enemy ? 2 : 0), &index);
    if (!next || !roomFor(*next, nullptr)) {
        return false;
    }
    const SmbArea current = area();
    if (next->objects == current.objects && next->enemies == current.enemies) {
        return false;
    }
    dragArea_ = *next;
    ++dragSerial_;
    selectObject(QStringLiteral("%1:%2").arg(enemy ? QStringLiteral("enemy") : QStringLiteral("object")).arg(index));
    emit contentChanged();
    return true;
}

void SmbLevelModel::endDrag() {
    std::optional<SmbArea> moved = dragArea_;
    if (moved && moved->objects == dragOriginal_->objects && moved->enemies == dragOriginal_->enemies) {
        moved.reset();  // dropped where it started
    }
    const QString what = dragging_.startsWith(QLatin1String("enemy:")) ? QStringLiteral("enemy")
                                                                        : QStringLiteral("object");
    dragOriginal_.reset();
    dragArea_.reset();
    dragAreas_.clear();
    dragging_.clear();
    ++dragSerial_;
    if (moved) {
        QString error;
        if (!commitArea(*moved, tr("Move %1 in %2").arg(what, title()), &error)) {
            emit contentChanged();
        }
    } else {
        emit contentChanged();
    }
    selectedRecord_ = selectedRecord();
}

bool SmbLevelModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    if (libraryItem == QLatin1String("start")) {
        // The test start marker: where Play from selected puts the player. It is
        // not level data and takes no bytes.
        session_->setPlayerCell(area_, QPoint(std::clamp(cell.x(), 0, std::max(0, scene().width - 1)),
                                             std::clamp(cell.y(), 1, 12)));
        session_->selectStart(area_);
        selectObject(QStringLiteral("player"));
        return true;
    }
    SmbArea next = area();
    const int page = std::clamp(cell.x() / 16, 0, kLastPage);
    const int column = cell.x() % 16;
    const int objectKind = recordIndex(libraryItem, "add-object:");
    const int enemyKind = recordIndex(libraryItem, "add-enemy:");
    QString id;
    QString name;
    if (objectKind >= 0 && objectKind < objectCatalog().size()) {
        const SmbObjectKind kind = objectCatalog().at(objectKind);
        const SmbAreaObject record = newObject(kind, column, std::clamp(cell.y(), 0, 11));
        id = QStringLiteral("object:%1").arg(insertObject(next.objects, page, record));
        name = kind.name;
    } else if (enemyKind >= 0) {
        // The sprite's top is in the cell it is dropped on, as when dragged.
        const SmbEnemyRecord record{QStringLiteral("enemy"), column, std::clamp(cell.y() + 2, 2, 13), false, false,
                                    enemyKind, -1, -1};
        id = QStringLiteral("enemy:%1").arg(insertEnemy(next.enemies, page, record));
        name = enemyName(enemyKind);
    } else {
        if (error) {
            *error = tr("Pick an object or an enemy in the library first.");
        }
        return false;
    }
    if (!commitArea(next, tr("Add %1").arg(name), error)) {
        return false;
    }
    selectObject(id);
    return true;
}

bool SmbLevelModel::removeObject(const QString& id, QString* error) {
    SmbArea next = area();
    const int o = recordIndex(id, "object:");
    const int e = recordIndex(id, "enemy:");
    QString name;
    if (o >= 0 && o < next.objects.size()) {
        name = areaObjectName(next.objects.at(o).row, next.objects.at(o).control);
        smb::removeObject(next.objects, o);
    } else if (e >= 0 && e < next.enemies.size()) {
        name = enemyName(next.enemies.at(e).objectOrPage);
        removeEnemy(next.enemies, e);
    } else {
        if (error && id == QLatin1String("player")) {
            *error = tr("The player is the start point; move it instead.");
        }
        return false;
    }
    if (!commitArea(next, tr("Delete %1").arg(name), error)) {
        return false;
    }
    selectObject(QString());
    return true;
}

bool SmbLevelModel::setField(const QString& key, const QVariant& value, QString* error) {
    SmbArea next = area();
    const int o = recordIndex(selected_, "object:");
    const int e = recordIndex(selected_, "enemy:");
    if (o >= 0 && o < next.objects.size()) {
        SmbAreaObject& record = next.objects[o];
        if (key == QLatin1String("kind")) {
            const int cls = objectClass(record);
            const int length = hasLength(record) ? (record.control & lengthMask(record)) : 0;
            for (const SmbObjectKind& kind : objectCatalog()) {
                if (kind.row == cls && kind.name == value.toString()) {
                    record.control = kind.control;
                    if (hasLength(record)) {
                        record.control |= length & lengthMask(record);
                    }
                }
            }
        } else if (key == QLatin1String("length") && hasLength(record)) {
            const int mask = lengthMask(record);
            record.control = (record.control & ~mask) | (std::clamp(value.toInt(), 1, mask + 1) - 1);
        } else if (key == QLatin1String("row") && record.row <= 11) {
            record.row = std::clamp(value.toInt(), 0, 11);
        } else {
            return TileMapModel::setField(key, value, error);
        }
        return commitArea(next, tr("Edit %1").arg(areaObjectName(record.row, record.control)), error);
    }
    if (e >= 0 && e < next.enemies.size()) {
        SmbEnemyRecord& record = next.enemies[e];
        if (key == QLatin1String("kind")) {
            for (const SmbEnemyKind& kind : enemyCatalog()) {
                if (kind.name == value.toString()) {
                    record.objectOrPage = kind.id;
                }
            }
        } else if (key == QLatin1String("hard")) {
            record.hardMode = value.toString() == QLatin1String("yes");
        } else if (key == QLatin1String("row")) {
            record.row = std::clamp(value.toInt(), 0, 13);
        } else {
            return TileMapModel::setField(key, value, error);
        }
        return commitArea(next, tr("Edit %1").arg(enemyName(record.objectOrPage)), error);
    }
    static const QStringList kHeader{QStringLiteral("timer"), QStringLiteral("entrance"), QStringLiteral("foreground"),
                                     QStringLiteral("style"), QStringLiteral("background"), QStringLiteral("terrain")};
    static const QVector<int> kMaximum{3, 7, 7, 3, 3, 15};
    const int field = kHeader.indexOf(key);
    if (field < 0) {
        return TileMapModel::setField(key, value, error);
    }
    next.header[field] = field == 0 ? std::max(0, int(timers().indexOf(value.toString())))
                                    : std::clamp(value.toInt(), 0, kMaximum.at(field));
    return commitArea(next, tr("Edit %1 header").arg(title()), error);
}

// ---------------------------------------------------------------- inspector

sdk::Library SmbLevelModel::library() const {
    if (libraryRevision_ != session_->revision()) {
        library_.clear();
        const SmbArea a = area();
        const QString type = session_->paletteKey(area_);
        const bool night = session_->night(area_);
        const SmbLevelRendering rendering = session_->levelRendering();
        // Each object alone on a bare area, against the bare area itself.
        const QVector<int> header{a.header.value(0), 0, 0, 0, 0, 0};
        QImage blank;
        try {
            blank = session_->painter().render(renderSmbLevelScene(area_, header, {}, {}, 0, rendering), type, night);
        } catch (const std::exception&) {
        }
        sdk::LibraryItem start;
        start.id = QStringLiteral("start");
        start.label = tr("Test start");
        start.hint = tr("drop where Play from selected (F4) puts the player; not level data");
        start.icon = session_->painter().player();
        library_ << start;
        const QVector<SmbObjectKind>& objects = objectCatalog();
        for (int i = 0; i < objects.size(); ++i) {
            const SmbObjectKind& kind = objects.at(i);
            sdk::LibraryItem item;
            item.id = QStringLiteral("add-object:%1").arg(i);
            item.label = kind.name;
            item.hint = kind.row == 8 ? QStringLiteral("object") : tr("row %1").arg(kind.row);
            try {
                const SmbLevelScene scene = renderSmbLevelScene(area_, header, {newObject(kind, 1, 6)}, {}, 0, rendering);
                const QImage image = session_->painter().render(scene, type, night);
                if (!blank.isNull() && image.size() == blank.size()) {
                    item.icon = difference(image, blank);
                }
            } catch (const std::exception&) {
            }
            library_ << item;
        }
        for (const SmbEnemyKind& kind : enemyCatalog()) {
            sdk::LibraryItem item;
            item.id = QStringLiteral("add-enemy:%1").arg(kind.id);
            item.label = kind.name;
            item.hint = QStringLiteral("enemy");
            item.icon = session_->painter().enemy(kind.id, type);
            library_ << item;
        }
        libraryRevision_ = session_->revision();
    }
    sdk::Library library;
    const SmbArea a = area();
    const SmbLevelPool pool = this->pool();
    const bool full = pool.pooled ? pool.objectSize - pool.objectUsed < 2
                                  : a.areaBytes() >= a.areaCapacity + (a.areaImplicitTerminator ? 1 : 0);
    library.hint = full ? (pool.pooled ? tr("levels full: delete something in any level")
                                       : tr("area full: delete something to add"))
                        : tr("drag onto the level, or pick and use the Add tool");
    library.items = library_;
    return library;
}

QImage SmbLevelModel::libraryIcon(const QString& id) const {
    library();
    for (const sdk::LibraryItem& item : library_) {
        if (item.id == id) return item.icon;
    }
    return {};
}

QString SmbLevelModel::startPoint() const {
    const SmbStart start = session_->start();
    if (start.area != area_) {
        return {};
    }
    return tr("World %1-%2 · column %3").arg(start.world).arg(start.level).arg(start.cell.x());
}

sdk::Budget SmbLevelModel::budget() const {
    const SmbArea a = area();
    sdk::Budget budget;
    const SmbLevelPool pool = this->pool();
    if (pool.pooled) {
        // The bar shows the ranges all levels share.
        budget.used = pool.objectUsed;
        budget.capacity = pool.objectSize;
        budget.text = tr("this level %1 + %2 B · all levels: objects %3 / %4, enemies %5 / %6")
                          .arg(a.areaBytes())
                          .arg(a.enemyBytes())
                          .arg(pool.objectUsed)
                          .arg(pool.objectSize)
                          .arg(pool.enemyUsed)
                          .arg(pool.enemySize);
        return budget;
    }
    budget.used = a.areaBytes();
    budget.capacity = a.areaCapacity + (a.areaImplicitTerminator ? 1 : 0);
    budget.text = tr("objects %1 / %2 · enemies %3 / %4 bytes")
                      .arg(a.areaBytes())
                      .arg(a.areaCapacity)
                      .arg(a.enemyBytes())
                      .arg(a.enemyCapacity);
    return budget;
}

QVector<sdk::ViewOption> SmbLevelModel::viewOptions() const {
    sdk::ViewOption lighting;
    lighting.key = QStringLiteral("lighting");
    lighting.label = tr("Lighting");
    lighting.value = session_->lighting();
    lighting.choices = QStringList{QStringLiteral("Auto"), QStringLiteral("Day"), QStringLiteral("Night")};
    return {lighting};
}

void SmbLevelModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("lighting")) {
        session_->setLighting(value);
        emit toolsChanged();
    }
}

QString SmbLevelModel::statusText() const {
    const QPair<int, int> course = session_->levelOf(area_);
    return course.first > 0 ? tr("%1 · World %2-%3 · %4 columns")
                                  .arg(title())
                                  .arg(course.first)
                                  .arg(course.second)
                                  .arg(scene().width)
                            : tr("%1 · %2 columns").arg(title()).arg(scene().width);
}

sdk::Inspection SmbLevelModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("smb.levels.v1");
    inspection.library = library();
    const SmbArea a = area();
    const int o = recordIndex(selected_, "object:");
    const int e = recordIndex(selected_, "enemy:");
    if (o >= 0 && o < a.objects.size()) {
        const SmbAreaObject& record = a.objects.at(o);
        const int page = objectPages(a.objects).at(o);
        inspection.kicker = tr("Area object");
        inspection.title = areaObjectName(record.row, record.control);
        const int cls = objectClass(record);
        if (cls >= 0) {
            QStringList kinds;
            QVector<QImage> icons;
            const QVector<SmbObjectKind> catalog = objectCatalog();
            for (int i = 0; i < catalog.size(); ++i) {
                if (catalog.at(i).row != cls) continue;
                kinds << catalog.at(i).name;
                icons << libraryIcon(QStringLiteral("add-object:%1").arg(i));
            }
            sdk::Field kind = choice(QStringLiteral("kind"), tr("Kind"), inspection.title, kinds);
            kind.choiceIcons = icons;
            inspection.fields << kind;
        }
        if (hasLength(record)) {
            inspection.fields << integer(QStringLiteral("length"), tr("Size"),
                                         (record.control & lengthMask(record)) + 1, 1, lengthMask(record) + 1,
                                         QStringLiteral("cells"));
        }
        if (record.row <= 11) {
            inspection.fields << integer(QStringLiteral("row"), tr("Row"), record.row, 0, 11);
        }
        inspection.fields << readOnly(QStringLiteral("column"), tr("Column"),
                                      tr("%1 (page %2, column %3)").arg(page * 16 + record.column).arg(page)
                                          .arg(record.column),
                                      true);
        inspection.note = tr("Drag to move it, Delete to remove it. The game reads the stream in page "
                             "and column order; moving a record keeps the others where they are.");
        inspection.engine = {{tr("Record"), tr("%1 of %2").arg(o).arg(a.objects.size())},
                             {tr("Bytes"), hex((record.column << 4) | record.row) + QLatin1Char(' ') +
                                                           hex((record.pageAdvance ? 0x80 : 0) | record.control)}};
        return inspection;
    }
    if (e >= 0 && e < a.enemies.size()) {
        const SmbEnemyRecord& record = a.enemies.at(e);
        const int page = enemyPages(a.enemies).at(e);
        inspection.kicker = tr("Enemy");
        inspection.title = enemyName(record.objectOrPage);
        QStringList kinds;
        QVector<QImage> icons;
        for (const SmbEnemyKind& kind : enemyCatalog()) {
            kinds << kind.name;
            icons << libraryIcon(QStringLiteral("add-enemy:%1").arg(kind.id));
        }
        if (!kinds.contains(inspection.title)) {
            kinds.prepend(inspection.title);
            icons.prepend(QImage());
        }
        sdk::Field kind = choice(QStringLiteral("kind"), tr("Kind"), inspection.title, kinds);
        kind.choiceIcons = icons;
        inspection.fields = {kind,
                             choice(QStringLiteral("hard"), tr("Hard mode only"),
                                    yesNo().at(record.hardMode ? 1 : 0), yesNo()),
                             integer(QStringLiteral("row"), tr("Row"), record.row, 0, 13),
                             readOnly(QStringLiteral("column"), tr("Column"),
                                      tr("%1 (page %2, column %3)").arg(page * 16 + record.column).arg(page)
                                          .arg(record.column),
                                      true)};
        inspection.note = tr("Hard-mode enemies appear from world 5 on and on a second quest.");
        inspection.engine = {{tr("Record"), tr("%1 of %2").arg(e).arg(a.enemies.size())},
                             {tr("Object"), hex(record.objectOrPage)}};
        return inspection;
    }
    if (selected_ == QLatin1String("player")) {
        const SmbStart start = session_->start();
        const QPoint cell = session_->playerCell(area_);
        inspection.kicker = tr("Start point");
        inspection.title = tr("Player");
        inspection.fields = {readOnly(QStringLiteral("column"), tr("Column"), cell.x()),
                             readOnly(QStringLiteral("row"), tr("Row"), cell.y()),
                             readOnly(QStringLiteral("page"), tr("Page"), cell.x() / 16),
                             readOnly(QStringLiteral("course"), tr("Plays as"),
                                      tr("World %1-%2").arg(start.world).arg(start.level))};
        inspection.note = tr("Drag the player to where Play from selected (F4) puts it. The start point "
                             "is a playtest setting, not level data.");
        return inspection;
    }
    inspection.kicker = tr("Area");
    inspection.title = title();
    const QVector<int>& h = a.header;
    inspection.fields = {choice(QStringLiteral("timer"), tr("Timer"), timers().value(h.value(0)), timers()),
                         integer(QStringLiteral("entrance"), tr("Entrance"), h.value(1), 0, 7),
                         integer(QStringLiteral("foreground"), tr("Foreground"), h.value(2), 0, 7),
                         integer(QStringLiteral("style"), tr("Style"), h.value(3), 0, 3),
                         integer(QStringLiteral("background"), tr("Background"), h.value(4), 0, 3),
                         integer(QStringLiteral("terrain"), tr("Terrain"), h.value(5), 0, 15)};
    inspection.note = tr("Drag objects and enemies onto the level from the library. Foreground 4-7 "
                         "set the sky colour (night, snow); terrain picks the floor and ceiling.");
    inspection.engine = {{tr("Objects"), tr("%1 records · %2 / %3 bytes")
                                                         .arg(a.objects.size())
                                                         .arg(a.areaBytes())
                                                         .arg(a.areaCapacity)},
                         {tr("Enemies"), tr("%1 records · %2 / %3 bytes")
                                                         .arg(a.enemies.size())
                                                         .arg(a.enemyBytes())
                                                         .arg(a.enemyCapacity)}};
    return inspection;
}

}  // namespace rl::smb
