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
    return session_->document().area(session_->levels(), area_);
}

const SmbLevelScene& SmbLevelModel::scene() const {
    if (sceneRevision_ != session_->revision()) {
        const SmbResourceDocument& d = session_->document();
        const SmbArea a = area();
        const QPair<int, int> course = session_->courseOf(area_);
        scene_ = renderSmbLevelScene(area_, a.header, a.objects, a.enemies, std::max(0, course.first - 1),
                                     SmbLevelRendering::fromId(d.rendering(session_->levels())));
        sceneRevision_ = session_->revision();
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
    return {{QStringLiteral("select"), QStringLiteral("Select and move (Hero: the start point)"),
             QStringLiteral("select")},
            {QStringLiteral("place"), QStringLiteral("Add the library item"), QStringLiteral("place")},
            {QStringLiteral("remove"), QStringLiteral("Delete objects and enemies"), QStringLiteral("erase")}};
}

QImage SmbLevelModel::render() const {
    const QString type = session_->paletteKey(area_);
    QImage image = session_->painter().render(scene(), type, session_->night(area_));
    const QImage hero = session_->painter().hero();
    if (!hero.isNull()) {
        QPainter painter(&image);
        painter.drawImage(session_->painter().heroBounds(session_->heroCell(area_)).topLeft(), hero);
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
    sdk::MapObject hero;
    hero.id = QStringLiteral("hero");
    hero.label = QStringLiteral("Hero · start point");
    hero.cells = cellsOf(session_->painter().heroBounds(session_->heroCell(area_)));
    hero.movable = true;
    result << hero;
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
    if (editing_ || !dragging_.isEmpty() || selected_.isEmpty() || selected_ == QLatin1String("hero")) {
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

bool SmbLevelModel::commitArea(const SmbArea& edited, const QString& text, QString* error) {
    if (!edited.areaFits()) {
        if (error) {
            *error = QStringLiteral("%1 has no room: its objects would take %2 of the %3 bytes the ROM keeps "
                                    "for them. Each area has a fixed space; delete or shorten an object first.")
                         .arg(title())
                         .arg(edited.areaBytes())
                         .arg(edited.areaCapacity);
        }
        return false;
    }
    if (!edited.enemiesFit()) {
        if (error) {
            *error = QStringLiteral("%1 has no room: its enemies would take %2 of the %3 bytes the ROM keeps "
                                    "for them. Each area has a fixed space; delete an enemy first.")
                         .arg(title())
                         .arg(edited.enemyBytes())
                         .arg(edited.enemyCapacity);
        }
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
    dragChanged_ = false;
    if (id == QLatin1String("hero")) {
        const QPoint feet = session_->heroCell(area_);
        dragOffset_ = feet - cellsOf(session_->painter().heroBounds(feet)).topLeft();
        return;
    }
    before_ = session_->document();
    beforeRevision_ = session_->revision();
}

bool SmbLevelModel::dragObject(const QString& id, QPoint topLeftCell) {
    if (id == QLatin1String("hero")) {
        // The start point is not level data: it moves without an undo step.
        const QPoint feet(std::clamp(topLeftCell.x() + dragOffset_.x(), 0, std::max(0, scene().width - 1)),
                          std::clamp(topLeftCell.y() + dragOffset_.y(), 1, 12));
        if (feet == session_->heroCell(area_)) {
            return false;
        }
        session_->setHeroCell(area_, feet);
        return true;
    }
    if (!before_ || id != dragging_) {
        return false;
    }
    // From the state the drag started in, so the record moves as one piece.
    const SmbArea original = before_->area(session_->levels(), area_);
    const bool enemy = id.startsWith(QLatin1String("enemy:"));
    int index = -1;
    // An enemy record's row is two below the cell its sprite starts in.
    const std::optional<SmbArea> next =
        moved(original, id, topLeftCell.x(), topLeftCell.y() + (enemy ? 2 : 0), &index);
    if (!next || !next->areaFits() || !next->enemiesFit()) {
        return false;
    }
    const SmbArea current = area();
    if (next->objects == current.objects && next->enemies == current.enemies) {
        return false;
    }
    SmbResourceDocument document = *before_;
    document.setArea(session_->levels(), *next);
    session_->setLive(document);
    dragChanged_ = true;
    selectObject(QStringLiteral("%1:%2").arg(enemy ? QStringLiteral("enemy") : QStringLiteral("object")).arg(index));
    return true;
}

void SmbLevelModel::endDrag() {
    if (before_ && dragChanged_) {
        const QString what = dragging_.startsWith(QLatin1String("enemy:")) ? QStringLiteral("enemy")
                                                                            : QStringLiteral("object");
        editing_ = true;
        session_->commit(*before_, beforeRevision_, QStringLiteral("Move %1 in %2").arg(what, title()));
        editing_ = false;
    }
    before_.reset();
    dragging_.clear();
    dragChanged_ = false;
    selectedRecord_ = selectedRecord();
}

bool SmbLevelModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    if (libraryItem == QLatin1String("start")) {
        // The test start marker: where Play from selected puts Hero. It is
        // not level data and takes no bytes.
        session_->setHeroCell(area_, QPoint(std::clamp(cell.x(), 0, std::max(0, scene().width - 1)),
                                             std::clamp(cell.y(), 1, 12)));
        session_->selectStart(area_);
        selectObject(QStringLiteral("hero"));
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
        const SmbObjectKind& kind = objectCatalog().at(objectKind);
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
            *error = QStringLiteral("Pick an object or an enemy in the library first.");
        }
        return false;
    }
    if (!commitArea(next, QStringLiteral("Add %1").arg(name), error)) {
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
        if (error && id == QLatin1String("hero")) {
            *error = QStringLiteral("Hero is the start point; move him instead.");
        }
        return false;
    }
    if (!commitArea(next, QStringLiteral("Delete %1").arg(name), error)) {
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
            const int length = hasLength(record) ? (record.control & 15) : 0;
            for (const SmbObjectKind& kind : objectCatalog()) {
                if (kind.row == cls && kind.name == value.toString()) {
                    record.control = kind.control;
                    if (hasLength(record)) {
                        record.control |= length;
                    }
                }
            }
        } else if (key == QLatin1String("length") && hasLength(record)) {
            record.control = (record.control & ~15) | (std::clamp(value.toInt(), 1, 16) - 1);
        } else if (key == QLatin1String("row") && record.row <= 11) {
            record.row = std::clamp(value.toInt(), 0, 11);
        } else {
            return TileMapModel::setField(key, value, error);
        }
        return commitArea(next, QStringLiteral("Edit %1").arg(areaObjectName(record.row, record.control)), error);
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
        return commitArea(next, QStringLiteral("Edit %1").arg(enemyName(record.objectOrPage)), error);
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
    return commitArea(next, QStringLiteral("Edit %1 header").arg(title()), error);
}

// ---------------------------------------------------------------- inspector

sdk::Library SmbLevelModel::library() const {
    if (libraryRevision_ != session_->revision()) {
        library_.clear();
        const SmbArea a = area();
        const QString type = session_->paletteKey(area_);
        const bool night = session_->night(area_);
        const SmbLevelRendering rendering = SmbLevelRendering::fromId(session_->document().rendering(session_->levels()));
        // Each object alone on a bare area, against the bare area itself.
        const QVector<int> header{a.header.value(0), 0, 0, 0, 0, 0};
        QImage blank;
        try {
            blank = session_->painter().render(renderSmbLevelScene(area_, header, {}, {}, 0, rendering), type, night);
        } catch (const std::exception&) {
        }
        sdk::LibraryItem start;
        start.id = QStringLiteral("start");
        start.label = QStringLiteral("Test start");
        start.hint = QStringLiteral("drop where Play from selected (F5) puts Hero; not level data");
        start.icon = session_->painter().hero();
        library_ << start;
        const QVector<SmbObjectKind>& objects = objectCatalog();
        for (int i = 0; i < objects.size(); ++i) {
            const SmbObjectKind& kind = objects.at(i);
            sdk::LibraryItem item;
            item.id = QStringLiteral("add-object:%1").arg(i);
            item.label = kind.name;
            item.hint = kind.row == 8 ? QStringLiteral("object") : QStringLiteral("row %1").arg(kind.row);
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
    library.hint = a.areaBytes() >= a.areaCapacity + (a.areaImplicitTerminator ? 1 : 0)
                       ? QStringLiteral("area full: delete something to add")
                       : QStringLiteral("drag onto the level, or pick and use the Add tool");
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
    return QStringLiteral("World %1-%2 · column %3").arg(start.world).arg(start.level).arg(start.cell.x());
}

sdk::Budget SmbLevelModel::budget() const {
    const SmbArea a = area();
    sdk::Budget budget;
    budget.used = a.areaBytes();
    budget.capacity = a.areaCapacity + (a.areaImplicitTerminator ? 1 : 0);
    budget.text = QStringLiteral("objects %1 / %2 · enemies %3 / %4 bytes")
                      .arg(a.areaBytes())
                      .arg(a.areaCapacity)
                      .arg(a.enemyBytes())
                      .arg(a.enemyCapacity);
    return budget;
}

QVector<sdk::ViewOption> SmbLevelModel::viewOptions() const {
    sdk::ViewOption lighting;
    lighting.key = QStringLiteral("lighting");
    lighting.label = QStringLiteral("Lighting");
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
    return course.first > 0 ? QStringLiteral("%1 · World %2-%3 · %4 columns")
                                  .arg(title())
                                  .arg(course.first)
                                  .arg(course.second)
                                  .arg(scene().width)
                            : QStringLiteral("%1 · %2 columns").arg(title()).arg(scene().width);
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
        inspection.kicker = QStringLiteral("Area object");
        inspection.title = areaObjectName(record.row, record.control);
        const int cls = objectClass(record);
        if (cls >= 0) {
            QStringList kinds;
            QVector<QImage> icons;
            for (int i = 0; i < objectCatalog().size(); ++i) {
                if (objectCatalog().at(i).row != cls) continue;
                kinds << objectCatalog().at(i).name;
                icons << libraryIcon(QStringLiteral("add-object:%1").arg(i));
            }
            sdk::Field kind = choice(QStringLiteral("kind"), QStringLiteral("Kind"), inspection.title, kinds);
            kind.choiceIcons = icons;
            inspection.fields << kind;
        }
        if (hasLength(record)) {
            inspection.fields << integer(QStringLiteral("length"), QStringLiteral("Size"), (record.control & 15) + 1,
                                         1, 16, QStringLiteral("cells"));
        }
        if (record.row <= 11) {
            inspection.fields << integer(QStringLiteral("row"), QStringLiteral("Row"), record.row, 0, 11);
        }
        inspection.fields << readOnly(QStringLiteral("column"), QStringLiteral("Column"),
                                      QStringLiteral("%1 (page %2, column %3)").arg(page * 16 + record.column).arg(page)
                                          .arg(record.column),
                                      true);
        inspection.note = QStringLiteral("Drag to move it, Delete to remove it. The game reads the stream in page "
                                         "and column order; moving a record keeps the others where they are.");
        inspection.engine = {{QStringLiteral("Record"), QStringLiteral("%1 of %2").arg(o).arg(a.objects.size())},
                             {QStringLiteral("Bytes"), hex((record.column << 4) | record.row) + QLatin1Char(' ') +
                                                           hex((record.pageAdvance ? 0x80 : 0) | record.control)}};
        return inspection;
    }
    if (e >= 0 && e < a.enemies.size()) {
        const SmbEnemyRecord& record = a.enemies.at(e);
        const int page = enemyPages(a.enemies).at(e);
        inspection.kicker = QStringLiteral("Enemy");
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
        sdk::Field kind = choice(QStringLiteral("kind"), QStringLiteral("Kind"), inspection.title, kinds);
        kind.choiceIcons = icons;
        inspection.fields = {kind,
                             choice(QStringLiteral("hard"), QStringLiteral("Hard mode only"),
                                    yesNo().at(record.hardMode ? 1 : 0), yesNo()),
                             integer(QStringLiteral("row"), QStringLiteral("Row"), record.row, 0, 13),
                             readOnly(QStringLiteral("column"), QStringLiteral("Column"),
                                      QStringLiteral("%1 (page %2, column %3)").arg(page * 16 + record.column).arg(page)
                                          .arg(record.column),
                                      true)};
        inspection.note = QStringLiteral("Hard-mode enemies appear from world 5 on and on a second quest.");
        inspection.engine = {{QStringLiteral("Record"), QStringLiteral("%1 of %2").arg(e).arg(a.enemies.size())},
                             {QStringLiteral("Object"), hex(record.objectOrPage)}};
        return inspection;
    }
    if (selected_ == QLatin1String("hero")) {
        const SmbStart start = session_->start();
        const QPoint cell = session_->heroCell(area_);
        inspection.kicker = QStringLiteral("Start point");
        inspection.title = QStringLiteral("Hero");
        inspection.fields = {readOnly(QStringLiteral("column"), QStringLiteral("Column"), cell.x()),
                             readOnly(QStringLiteral("row"), QStringLiteral("Row"), cell.y()),
                             readOnly(QStringLiteral("page"), QStringLiteral("Page"), cell.x() / 16),
                             readOnly(QStringLiteral("course"), QStringLiteral("Plays as"),
                                      QStringLiteral("World %1-%2").arg(start.world).arg(start.level))};
        inspection.note = QStringLiteral("Drag Hero to where Play from selected (F5) puts him. The start point "
                                         "is a playtest setting, not level data.");
        return inspection;
    }
    inspection.kicker = QStringLiteral("Area");
    inspection.title = title();
    const QVector<int>& h = a.header;
    inspection.fields = {choice(QStringLiteral("timer"), QStringLiteral("Timer"), timers().value(h.value(0)), timers()),
                         integer(QStringLiteral("entrance"), QStringLiteral("Entrance"), h.value(1), 0, 7),
                         integer(QStringLiteral("foreground"), QStringLiteral("Foreground"), h.value(2), 0, 7),
                         integer(QStringLiteral("style"), QStringLiteral("Style"), h.value(3), 0, 3),
                         integer(QStringLiteral("background"), QStringLiteral("Background"), h.value(4), 0, 3),
                         integer(QStringLiteral("terrain"), QStringLiteral("Terrain"), h.value(5), 0, 15)};
    inspection.note = QStringLiteral("Drag objects and enemies onto the level from the library. Foreground 4-7 "
                                     "set the sky colour (night, snow); terrain picks the floor and ceiling.");
    inspection.engine = {{QStringLiteral("Objects"), QStringLiteral("%1 records · %2 / %3 bytes")
                                                         .arg(a.objects.size())
                                                         .arg(a.areaBytes())
                                                         .arg(a.areaCapacity)},
                         {QStringLiteral("Enemies"), QStringLiteral("%1 records · %2 / %3 bytes")
                                                         .arg(a.enemies.size())
                                                         .arg(a.enemyBytes())
                                                         .arg(a.enemyCapacity)}};
    return inspection;
}

}  // namespace rl::smb
