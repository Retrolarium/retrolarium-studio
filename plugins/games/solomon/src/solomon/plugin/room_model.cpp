#include "solomon/plugin/room_model.hpp"

#include "nes/editors.hpp"
#include "sdk/i18n.hpp"
#include "solomon/data/names.hpp"
#include "solomon/plugin/solomon_session.hpp"

#include <QPainter>

#include <algorithm>

namespace rl::solomon {

namespace {

constexpr int kEmptyPattern = 0x10;
constexpr int kBrownPattern = 0x00;
constexpr int kWhitePattern = 0x03;

const QStringList& metadataKeys() {
    static const QStringList kKeys{QStringLiteral("door"), QStringLiteral("key"), QStringLiteral("mirror_1"),
                                   QStringLiteral("mirror_2")};
    return kKeys;
}

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

sdk::Field choice(const QString& key, const QString& label, const QString& value, const QStringList& choices,
                  const QVector<QImage>& icons = {}) {
    sdk::Field field;
    field.key = key;
    field.label = label;
    field.type = sdk::Field::Type::Choice;
    field.value = value;
    field.choices = choices;
    field.choiceIcons = icons;
    return field;
}

int hexOf(const QString& text) {
    bool ok = false;
    const int value = text.section(QLatin1Char('$'), 1).left(2).toInt(&ok, 16);
    return ok ? value : -1;
}

QString metadataLabel(const QString& key) {
    if (key == QLatin1String("door")) return rlTr("solomon", "Door");
    if (key == QLatin1String("key")) return rlTr("solomon", "Key");
    if (key == QLatin1String("mirror_1")) return rlTr("solomon", "Spawn Mirror 1");
    if (key == QLatin1String("mirror_2")) return rlTr("solomon", "Spawn Mirror 2");
    if (key == QLatin1String("player_start")) return rlTr("solomon", "Wizard · start");
    return key;
}

const QStringList& hidings() {
    static const QStringList kHidings{rlTr("solomon", "In the open"), rlTr("solomon", "Hidden"),
                                      rlTr("solomon", "In a brown block")};
    return kHidings;
}

}  // namespace

SolomonRoomModel::SolomonRoomModel(SolomonSession* session, int room)
    : TileMapModel(session), session_(session), room_(room) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &SolomonSession::startChanged, this, &sdk::EditorModel::toolsChanged);
    session_->setStartRoom(room_ + 1);
}

const SolomonRooms& SolomonRoomModel::rooms() const {
    return draft_ ? *draft_ : session_->rooms();
}

int SolomonRoomModel::bank() const {
    return SolomonRooms::chrBank(rooms().itemCommands(room_));
}

int SolomonRoomModel::group() const {
    return session_->painter().groupOf(room_);
}

QString SolomonRoomModel::title() const {
    return roomLabel(session_->rooms().roomNumber(room_));
}

QVector<sdk::MapLayer> SolomonRoomModel::layers() const {
    return {{QStringLiteral("blocks"), tr("Blocks")},
            {QStringLiteral("items"), tr("Items")},
            {QStringLiteral("enemies"), tr("Enemies")}};
}

void SolomonRoomModel::setCurrentLayer(const QString& id) {
    if (id == layer_ || (id != QLatin1String("blocks") && id != QLatin1String("items") && id != QLatin1String("enemies"))) {
        return;
    }
    layer_ = id;
    selected_.clear();
    emit toolsChanged();
    emit selectionChanged();
    emit contentChanged();
}

QVector<sdk::Tool> SolomonRoomModel::tools() const {
    if (layer_ == QLatin1String("blocks")) {
        return {{QStringLiteral("paint"), tr("Paint blocks (right button takes a cell)"), QStringLiteral("paint")},
                {QStringLiteral("pick"), tr("Take a cell's blocks as the brush"), QStringLiteral("pick")}};
    }
    return {{QStringLiteral("select"), tr("Select and move"), QStringLiteral("select")},
            {QStringLiteral("place"), tr("Add the library item"), QStringLiteral("place")},
            {QStringLiteral("remove"), tr("Delete"), QStringLiteral("erase")}};
}

QImage SolomonRoomModel::render() const {
    return session_->painter().room(room_, SolomonPainter::Options(), draft_ ? &*draft_ : nullptr);
}

// ---------------------------------------------------------------- blocks

QVector<sdk::CellBrush> SolomonRoomModel::brushes() const {
    if (layer_ != QLatin1String("blocks")) return {};
    return {{0, tr("Empty"), QColor()}, {brush_, tr("Brush"), QColor()}};
}

void SolomonRoomModel::setCurrentBrush(int value) {
    brush_ = std::clamp(value, 0, 3);
    emit toolsChanged();
    emit selectionChanged();
}

int SolomonRoomModel::cellValue(QPoint cell) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= kWidth || cell.y() >= kHeight) return -1;
    return (rooms().block(room_, cell, false) ? 1 : 0) | (rooms().block(room_, cell, true) ? 2 : 0);
}

bool SolomonRoomModel::isCellEditable(QPoint cell) const {
    return layer_ == QLatin1String("blocks") && cellValue(cell) >= 0;
}

void SolomonRoomModel::beginStroke() {
    draft_ = session_->rooms();
}

bool SolomonRoomModel::paintCell(QPoint cell, int value) {
    if (!isCellEditable(cell) || value < 0 || value > 3 || cellValue(cell) == value) return false;
    if (!draft_) {
        SolomonRooms next = session_->rooms();
        next.setBlock(room_, cell, false, value & 1);
        next.setBlock(room_, cell, true, value & 2);
        return commit(next, tr("Paint blocks in %1").arg(title()), nullptr);
    }
    draft_->setBlock(room_, cell, false, value & 1);
    draft_->setBlock(room_, cell, true, value & 2);
    emit contentChanged();
    return true;
}

void SolomonRoomModel::endStroke() {
    if (!draft_) return;
    const SolomonRooms next = *draft_;
    draft_.reset();
    if (next != session_->rooms()) commit(next, tr("Paint blocks in %1").arg(title()), nullptr);
    emit contentChanged();
}

// ---------------------------------------------------------------- libraries

QString SolomonRoomModel::currentLibraryItem() const {
    if (layer_ == QLatin1String("blocks")) return QStringLiteral("block:%1").arg(brush_);
    if (layer_ == QLatin1String("enemies")) return QStringLiteral("enemy:%1").arg(enemyType_);
    return QStringLiteral("item:%1").arg(itemType_);
}

void SolomonRoomModel::setCurrentLibraryItem(const QString& id) {
    if (id.startsWith(QLatin1String("block:"))) {
        setCurrentBrush(id.mid(6).toInt());
    } else if (id.startsWith(QLatin1String("item:"))) {
        itemType_ = std::clamp(id.mid(5).toInt(), 1, 0xbf);
        emit selectionChanged();
    } else if (id.startsWith(QLatin1String("enemy:"))) {
        enemyType_ = std::clamp(id.mid(6).toInt(), 0x18, 0x83);
        emit selectionChanged();
    }
}

QImage SolomonRoomModel::itemIcon(int type) const {
    const int identity = type & 0x3f;
    return session_->painter().patternIcon(identity < patternCount(session_->state().art) ? identity : kEmptyPattern,
                                           bank(), group());
}

sdk::Library SolomonRoomModel::itemLibrary() const {
    const QString key = QStringLiteral("%1|%2|%3").arg(session_->revision()).arg(bank()).arg(group());
    if (key != libraryKey_) {
        items_.clear();
        enemies_.clear();
        libraryKey_ = key;
    }
    if (items_.isEmpty()) {
        const int count = std::min(0x40, patternCount(session_->state().art));
        for (int type = 1; type < count; ++type) {
            if (type == kEmptyPattern) continue;
            sdk::LibraryItem item;
            item.id = QStringLiteral("item:%1").arg(type);
            item.label = QStringLiteral("%1 · %2").arg(hex(type), itemName(type));
            item.icon = itemIcon(type);
            items_ << item;
        }
    }
    sdk::Library library;
    library.grid = true;
    library.hint = tr("click: pick · drag onto the room");
    library.items = items_;
    return library;
}

sdk::Library SolomonRoomModel::enemyLibrary() const {
    itemLibrary();      // refreshes the cache key
    if (enemies_.isEmpty()) {
        for (int type = 0x18; type <= 0x83; ++type) {
            const QImage icon = session_->painter().enemyIcon(type, bank());
            if (icon.isNull()) continue;
            sdk::LibraryItem item;
            item.id = QStringLiteral("enemy:%1").arg(type);
            item.label = QStringLiteral("%1 · %2").arg(hex(type), enemyName(type));
            item.icon = icon;
            enemies_ << item;
        }
    }
    sdk::Library library;
    library.grid = true;
    library.hint = tr("click: pick · drag onto the room");
    library.items = enemies_;
    return library;
}

sdk::Library SolomonRoomModel::sheet() const {
    if (layer_ == QLatin1String("items")) return itemLibrary();
    if (layer_ == QLatin1String("enemies")) return enemyLibrary();
    sdk::Library library;
    library.grid = true;
    library.hint = tr("click: brush · drag onto a cell");
    const SolomonPainter& p = session_->painter();
    const auto add = [&](int value, const QString& label, QImage icon) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("block:%1").arg(value);
        item.label = label;
        item.icon = icon;
        library.items << item;
    };
    add(0, tr("Empty"), p.patternIcon(kEmptyPattern, bank(), group()));
    add(1, tr("Brown block (breakable)"), p.patternIcon(kBrownPattern, bank(), group()));
    add(2, tr("White block (solid)"), p.patternIcon(kWhitePattern, bank(), group()));
    QImage both = p.patternIcon(kBrownPattern, bank(), group());
    {
        QPainter painter(&both);
        painter.setOpacity(0.5);
        painter.drawImage(0, 0, p.patternIcon(kWhitePattern, bank(), group(), true));
    }
    add(3, tr("White over brown"), both);
    return library;
}

// ---------------------------------------------------------------- objects

QVector<sdk::MapObject> SolomonRoomModel::objects() const {
    QVector<sdk::MapObject> result;
    if (layer_ == QLatin1String("blocks")) return result;
    const SolomonRooms& r = rooms();
    const auto add = [&result](const QString& id, const QString& label, QRect cells) {
        sdk::MapObject o;
        o.id = id;
        o.label = label;
        o.cells = cells;
        result << o;
    };
    if (layer_ == QLatin1String("items")) {
        const QJsonArray commands = r.itemCommands(room_);
        for (int c = 0; c < commands.size(); ++c) {
            const QJsonObject command = commands.at(c).toObject();
            const QString kind = command.value(QStringLiteral("kind")).toString();
            const int type = command.value(QStringLiteral("type")).toInt();
            if (kind == QLatin1String("item")) {
                add(QStringLiteral("item:%1").arg(c), itemTypeName(type),
                    QRect(point(command.value(QStringLiteral("position"))), QSize(1, 1)));
            } else if (kind == QLatin1String("repeat")) {
                const QJsonArray positions = command.value(QStringLiteral("positions")).toArray();
                for (int p = 0; p < positions.size(); ++p) {
                    add(QStringLiteral("repeat:%1:%2").arg(c).arg(p), itemTypeName(type),
                        QRect(point(positions.at(p)), QSize(1, 1)));
                }
            } else if (kind == QLatin1String("constellation")) {
                add(QStringLiteral("constellation:%1").arg(c),
                    tr("Constellation · %1").arg(constellationName(command.value(QStringLiteral("opcode")).toInt())),
                    QRect(point(command.value(QStringLiteral("position"))), QSize(3, 2)));
            }
        }
        for (const QString& key : metadataKeys()) {
            const QPoint p = r.metadataPosition(room_, key);
            if (p.x() >= 0) add(QStringLiteral("meta:") + key, metadataLabel(key), QRect(p, QSize(1, 1)));
        }
    } else {
        const QVector<SolomonEnemy> enemies = r.enemies(room_);
        for (int i = 0; i < enemies.size(); ++i) {
            add(QStringLiteral("enemy:%1").arg(i), enemyName(enemies.at(i).type),
                QRect(enemies.at(i).position, QSize(1, 1)));
        }
        const QPoint start = r.metadataPosition(room_, QStringLiteral("player_start"));
        if (start.x() >= 0) add(QStringLiteral("meta:player_start"), metadataLabel(QStringLiteral("player_start")), QRect(start, QSize(1, 1)));
    }
    return result;
}

void SolomonRoomModel::selectObject(const QString& id) {
    if (id != selected_) {
        selected_ = id;
        emit selectionChanged();
    }
}

bool SolomonRoomModel::moveTo(SolomonRooms& r, const QString& id, QPoint cell) const {
    const QStringList parts = id.split(QLatin1Char(':'));
    if (parts.first() == QLatin1String("meta")) {
        QJsonObject metadata = r.metadata(room_);
        if (point(metadata.value(parts.value(1))) == cell) return false;
        metadata.insert(parts.value(1), pointJson(cell));
        r.setMetadata(room_, metadata);
        return true;
    }
    if (parts.first() == QLatin1String("enemy")) {
        QVector<SolomonEnemy> enemies = r.enemies(room_);
        const int i = parts.value(1).toInt();
        if (i < 0 || i >= enemies.size() || enemies.at(i).position == cell) return false;
        enemies[i].position = cell;
        r.setEnemies(room_, enemies);
        return true;
    }
    QJsonArray commands = r.itemCommands(room_);
    const int c = parts.value(1).toInt();
    if (c < 0 || c >= commands.size()) return false;
    QJsonObject command = commands.at(c).toObject();
    // A selection from before an edit may name another command now: the
    // list's terminator among them.
    if (command.value(QStringLiteral("kind")).toString() != parts.first()) return false;
    if (parts.first() == QLatin1String("repeat")) {
        QJsonArray positions = command.value(QStringLiteral("positions")).toArray();
        const int p = parts.value(2).toInt();
        if (p < 0 || p >= positions.size() || point(positions.at(p)) == cell) return false;
        positions.replace(p, pointJson(cell));
        command.insert(QStringLiteral("positions"), positions);
    } else {
        if (point(command.value(QStringLiteral("position"))) == cell) return false;
        command.insert(QStringLiteral("position"), pointJson(cell));
    }
    commands.replace(c, command);
    r.setItemCommands(room_, commands);
    return true;
}

void SolomonRoomModel::beginDrag(const QString& id) {
    selectObject(id);
    draft_ = session_->rooms();
}

bool SolomonRoomModel::dragObject(const QString& id, QPoint topLeftCell) {
    if (!draft_) return false;
    const QSize size = id.startsWith(QLatin1String("constellation")) ? QSize(3, 2) : QSize(1, 1);
    const QPoint cell(std::clamp(topLeftCell.x(), 0, kWidth - size.width()),
                      std::clamp(topLeftCell.y(), 0, kHeight - size.height()));
    if (!moveTo(*draft_, id, cell)) return false;
    emit contentChanged();
    return true;
}

void SolomonRoomModel::endDrag() {
    if (!draft_) return;
    const SolomonRooms next = *draft_;
    draft_.reset();
    if (next != session_->rooms()) commit(next, tr("Move %1 in %2").arg(objectLabel(selected_), title()), nullptr);
    emit contentChanged();
}

bool SolomonRoomModel::commit(const SolomonRooms& next, const QString& text, QString* error) {
    // Refuse only what makes things worse: an edit that frees bytes is fine
    // even while the rooms are still over.
    const QString overflow = next.overflow();
    if (!overflow.isEmpty() && next.itemBytes() + next.enemyBytes() >=
                                   session_->rooms().itemBytes() + session_->rooms().enemyBytes()) {
        if (error) *error = overflow;
        return false;
    }
    return session_->edit(text, [next](SolomonState& s) { s.rooms = next; }, error);
}

bool SolomonRoomModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= kWidth || cell.y() >= kHeight) return false;
    SolomonRooms next = session_->rooms();
    if (libraryItem.startsWith(QLatin1String("block:"))) {
        setCurrentLibraryItem(libraryItem);
        if (layer_ != QLatin1String("blocks")) setCurrentLayer(QStringLiteral("blocks"));
        paintCell(cell, brush_);
        return true;
    }
    if (libraryItem.startsWith(QLatin1String("enemy:"))) {
        const int type = libraryItem.mid(6).toInt();
        QVector<SolomonEnemy> enemies = next.enemies(room_);
        if (enemies.size() >= kMaximumEnemies) {
            if (error) *error = tr("A room holds at most %1 enemies.").arg(kMaximumEnemies);
            return false;
        }
        enemies << SolomonEnemy{type, cell};
        next.setEnemies(room_, enemies);
        if (!commit(next, tr("Add %1 to %2").arg(enemyName(type), title()), error)) return false;
        setCurrentLayer(QStringLiteral("enemies"));
        selectObject(QStringLiteral("enemy:%1").arg(enemies.size() - 1));
        return true;
    }
    if (libraryItem.startsWith(QLatin1String("item:"))) {
        const int type = libraryItem.mid(5).toInt();
        QJsonArray commands = next.itemCommands(room_);
        QJsonObject command;
        command.insert(QStringLiteral("kind"), QStringLiteral("item"));
        command.insert(QStringLiteral("position"), pointJson(cell));
        command.insert(QStringLiteral("type"), type);
        // Items go before the terminator that ends the list.
        const int at = std::max(0, int(commands.size()) - 1);
        commands.insert(at, command);
        next.setItemCommands(room_, commands);
        if (!commit(next, tr("Add %1 to %2").arg(itemName(type), title()), error)) return false;
        setCurrentLayer(QStringLiteral("items"));
        selectObject(QStringLiteral("item:%1").arg(at));
        return true;
    }
    if (error) *error = tr("Pick something in the library first.");
    return false;
}

bool SolomonRoomModel::removeObject(const QString& id, QString* error) {
    const QStringList parts = id.split(QLatin1Char(':'));
    SolomonRooms next = session_->rooms();
    if (parts.first() == QLatin1String("enemy")) {
        QVector<SolomonEnemy> enemies = next.enemies(room_);
        const int i = parts.value(1).toInt();
        if (i < 0 || i >= enemies.size()) return false;
        enemies.removeAt(i);
        next.setEnemies(room_, enemies);
    } else if (parts.first() == QLatin1String("item") || parts.first() == QLatin1String("repeat")) {
        QJsonArray commands = next.itemCommands(room_);
        const int c = parts.value(1).toInt();
        if (c < 0 || c >= commands.size()) return false;
        QJsonObject command = commands.at(c).toObject();
        // A selection from before an edit may name another command now: the
        // list's terminator among them.
        if (command.value(QStringLiteral("kind")).toString() != parts.first()) return false;
        if (parts.first() == QLatin1String("item")) {
            commands.removeAt(c);
        } else {
            QJsonArray positions = command.value(QStringLiteral("positions")).toArray();
            const int p = parts.value(2).toInt();
            if (p < 0 || p >= positions.size()) return false;
            positions.removeAt(p);
            if (positions.size() == 1) {
                // One left: a plain item.
                QJsonObject item;
                item.insert(QStringLiteral("kind"), QStringLiteral("item"));
                item.insert(QStringLiteral("position"), positions.at(0));
                item.insert(QStringLiteral("type"), command.value(QStringLiteral("type")));
                commands.replace(c, item);
            } else {
                command.insert(QStringLiteral("positions"), positions);
                command.insert(QStringLiteral("opcode"), 0xc0 + positions.size() - 1);
                commands.replace(c, command);
            }
        }
        next.setItemCommands(room_, commands);
    } else {
        if (error) {
            *error = id.startsWith(QLatin1String("constellation"))
                         ? tr("The constellation ends the room's item list; move it instead.")
                         : tr("%1 stays in the room; move it instead.").arg(objectLabel(id));
        }
        return false;
    }
    selectObject(QString());
    return commit(next, tr("Delete %1 from %2").arg(objectLabel(id), title()), error);
}

QString SolomonRoomModel::objectLabel(const QString& id) const {
    for (const sdk::MapObject& o : objects()) {
        if (o.id == id) return o.label;
    }
    return id.startsWith(QLatin1String("meta:")) ? metadataLabel(id.mid(5)) : tr("an object");
}

// ---------------------------------------------------------------- inspector

sdk::Budget SolomonRoomModel::budget() const {
    const SolomonRooms& r = rooms();
    sdk::Budget budget;
    if (r.expanded()) {
        const int capacity = expandedDataBytes(r.revision());
        budget.used = r.itemBytes() + r.enemyBytes();
        budget.capacity = capacity;
        budget.text = tr("this room: items %1 / %2 B, enemies %3 / %4 · expanded ROM: %5 / %6 B")
                          .arg(SolomonRooms::itemStreamBytes(r.itemCommands(room_)))
                          .arg(kExpandedStreamLimit)
                          .arg(r.enemies(room_).size())
                          .arg(kMaximumEnemies)
                          .arg(budget.used)
                          .arg(capacity);
        return budget;
    }
    budget.used = r.itemBytes();
    budget.capacity = kItemArenaBytes;
    budget.text = tr("this room: items %1 B, enemies %2 / %3 · all rooms: items %4 / %5 B, enemies %6 / %7 B")
                      .arg(SolomonRooms::itemStreamBytes(r.itemCommands(room_)))
                      .arg(r.enemies(room_).size())
                      .arg(kMaximumEnemies)
                      .arg(r.itemBytes())
                      .arg(kItemArenaBytes)
                      .arg(r.enemyBytes())
                      .arg(kEnemyArenaBytes);
    return budget;
}

QString SolomonRoomModel::startPoint() const {
    return session_->startRoom() == room_ + 1 ? session_->selectedStart() : QString();
}

QString SolomonRoomModel::statusText() const {
    return tr("%1 · CHR bank %2 · palette group %3").arg(title()).arg(bank()).arg(group());
}

sdk::Inspection SolomonRoomModel::inspect() const {
    sdk::Inspection inspection;
    const SolomonRooms& r = rooms();
    const QStringList parts = selected_.split(QLatin1Char(':'));
    if (parts.first() == QLatin1String("item") || parts.first() == QLatin1String("repeat")) {
        const QJsonObject command = r.itemCommands(room_).at(parts.value(1).toInt()).toObject();
        const int type = command.value(QStringLiteral("type")).toInt();
        inspection.kicker = parts.first() == QLatin1String("repeat") ? tr("Repeated item") : tr("Item");
        inspection.title = itemName(type);
        QStringList choices;
        QVector<QImage> icons;
        for (const sdk::LibraryItem& item : itemLibrary().items) {
            choices << item.label;
            icons << item.icon;
        }
        const int hiding = type >= 0xc0 ? 0 : (type & 0x80) ? 2 : (type & 0x40) ? 1 : 0;
        inspection.fields = {choice(QStringLiteral("type"), tr("Item"),
                                    QStringLiteral("%1 · %2").arg(hex(type & 0x3f), itemName(type)), choices, icons),
                             choice(QStringLiteral("hiding"), tr("Where"), hidings().at(hiding), hidings())};
        inspection.note = parts.first() == QLatin1String("repeat")
                              ? tr("One of several copies the item list places with one command; they "
                                   "share the item. Delete one and the rest stay.")
                              : tr("Hidden items appear when Wizard finds them; an item in a brown block "
                                   "shows when the block breaks.");
        inspection.engineTag = QStringLiteral("solomon.rooms@3");
        inspection.engine = {{tr("Type byte"), hex(type)}};
        inspection.library = sdk::Library();
        return inspection;
    }
    if (parts.first() == QLatin1String("enemy")) {
        const QVector<SolomonEnemy> enemies = r.enemies(room_);
        const int i = parts.value(1).toInt();
        if (i >= 0 && i < enemies.size()) {
            inspection.kicker = tr("Enemy");
            inspection.title = enemyName(enemies.at(i).type);
            QStringList choices;
            QVector<QImage> icons;
            for (const sdk::LibraryItem& item : enemyLibrary().items) {
                choices << item.label;
                icons << item.icon;
            }
            inspection.fields = {choice(QStringLiteral("enemy"), tr("Type"),
                                        QStringLiteral("%1 · %2").arg(hex(enemies.at(i).type), enemyName(enemies.at(i).type)),
                                        choices, icons)};
            inspection.note = tr("The type byte holds the enemy, the way it faces and its speed; each "
                                 "choice names all three. A room holds at most 17 enemies.");
            inspection.engine = {{tr("Type byte"), hex(enemies.at(i).type)}};
            inspection.engineTag = QStringLiteral("solomon.rooms@3");
            return inspection;
        }
    }
    if (parts.first() == QLatin1String("meta")) {
        const QString key = parts.value(1);
        inspection.kicker = tr("Room fixture");
        inspection.title = metadataLabel(key);
        const QJsonObject metadata = r.metadata(room_);
        if (key == QLatin1String("key")) {
            const int status = metadata.value(QStringLiteral("status_rate_raw")).toInt();
            inspection.fields = {choice(QStringLiteral("key_status"), tr("Where"),
                                        hidings().at(status >= 0x80 ? 1 : status >= 0x40 ? 2 : 0), hidings())};
        } else if (key.startsWith(QLatin1String("mirror_"))) {
            const int set = metadata.value(key + QStringLiteral("_enemy_set")).toInt();
            const int schedule = metadata.value(key + QStringLiteral("_schedule")).toInt();
            QStringList sets;
            const QVector<SolomonMirrorSet> all = r.mirrorSets();
            for (int i = 0; i < all.size(); ++i) {
                QStringList names;
                for (int type : all.at(i).types) names << enemyName(type).section(QLatin1Char(' '), 0, 0);
                names.removeDuplicates();
                sets << tr("Set %1 · %2").arg(i).arg(names.join(QStringLiteral(", ")));
            }
            QStringList schedules;
            for (int i = 0; i < r.schedules().size(); ++i) schedules << tr("Schedule %1").arg(i);
            inspection.fields = {choice(QStringLiteral("enemy_set"), tr("Drops"), sets.value(set), sets),
                                 choice(QStringLiteral("schedule"), tr("When"), schedules.value(schedule), schedules)};
        }
        inspection.note = key == QLatin1String("player_start")
                              ? tr("Where Wizard appears; she faces the room's middle.")
                          : key.startsWith(QLatin1String("mirror_"))
                              ? tr("A Spawn Mirror drops the enemies of its set in turn, when its drop "
                                   "schedule says. Sets and schedules are shared by all rooms; edit them "
                                   "in Mirrors.")
                              : tr("Drag it to move it. Every room has one door, one key and its mirrors.");
        inspection.engineTag = QStringLiteral("solomon.rooms@3");
        return inspection;
    }
    if (parts.first() == QLatin1String("constellation")) {
        const int opcode = r.itemCommands(room_).at(parts.value(1).toInt()).toObject().value(QStringLiteral("opcode")).toInt();
        inspection.kicker = tr("Constellation");
        inspection.title = constellationName(opcode);
        QStringList names;
        for (int k = 0; k < 4; ++k) names << constellationName((opcode & ~3) | k);
        inspection.fields = {choice(QStringLiteral("constellation"), tr("Sign"), constellationName(opcode), names)};
        inspection.note = tr("The constellation sign ends the room's item list; each tileset has four. "
                             "Drag it to move it; the room inspector removes it.");
        return inspection;
    }
    inspection.kicker = tr("Room");
    inspection.title = title();
    const QJsonArray commands = r.itemCommands(room_);
    const QJsonObject last = commands.isEmpty() ? QJsonObject() : commands.last().toObject();
    QStringList signs{QStringLiteral("None")};
    for (int k = 0; k < 4 && bank() < 3; ++k) signs << constellationName(0xf0 | (bank() << 2) | k);
    const QString sign = last.value(QStringLiteral("kind")).toString() == QLatin1String("constellation")
                             ? constellationName(last.value(QStringLiteral("opcode")).toInt())
                             : QStringLiteral("None");
    inspection.fields = {choice(QStringLiteral("bank"), tr("Tileset"), QString::number(bank()),
                                {QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")}),
                         integer(QStringLiteral("time_rate"), tr("Timer speed"),
                                 r.metadata(room_).value(QStringLiteral("status_rate_raw")).toInt() & 0x0f, 0, 15),
                         integer(QStringLiteral("spawn_lifetime"), tr("Hornhead lifetime"),
                                 r.spawnLifetime(room_), 0, 255, tr("also Saramandor")),
                         choice(QStringLiteral("room_constellation"), tr("Constellation"), sign, signs),
                         choice(QStringLiteral("rom_layout"), QStringLiteral("ROM"),
                                r.expanded() ? QStringLiteral("Expanded") : tr("Original"),
                                {tr("Original"), QStringLiteral("Expanded")})};
    inspection.note = layer_ == QLatin1String("blocks")
                          ? tr("Brown blocks break and can be made by Wizard; white blocks are solid. "
                               "The right button takes a cell's blocks as the brush.")
                          : tr("Drag items and enemies from the sheet onto the room. The door, key, "
                               "mirrors and Wizard's start are moved, not deleted.");
    inspection.engineTag = QStringLiteral("solomon.rooms@3");
    inspection.engine = {{tr("Palette group"), QString::number(group())},
                         {tr("Item list"), tr("%n bytes", nullptr, SolomonRooms::itemStreamBytes(r.itemCommands(room_)))}};
    return inspection;
}

bool SolomonRoomModel::setField(const QString& key, const QVariant& value, QString* error) {
    SolomonRooms next = session_->rooms();
    const QStringList parts = selected_.split(QLatin1Char(':'));
    if (key == QLatin1String("type") || key == QLatin1String("hiding")) {
        QJsonArray commands = next.itemCommands(room_);
        const int c = parts.value(1).toInt();
        if (c < 0 || c >= commands.size()) return false;
        QJsonObject command = commands.at(c).toObject();
        int type = command.value(QStringLiteral("type")).toInt();
        int identity = type & 0x3f;
        int flags = type >= 0xc0 ? 0 : type & 0xc0;
        if (key == QLatin1String("type")) {
            identity = hexOf(value.toString());
            if (identity < 0) return false;
        } else {
            const int hiding = hidings().indexOf(value.toString());
            flags = hiding == 2 ? 0x80 : hiding == 1 ? 0x40 : 0;
        }
        type = identity | flags;
        if (type < 1 || type >= 0xc0) {
            if (error) *error = tr("That item cannot be stored that way.");
            return false;
        }
        command.insert(QStringLiteral("type"), type);
        commands.replace(c, command);
        next.setItemCommands(room_, commands);
        return commit(next, tr("Edit an item in %1").arg(title()), error);
    }
    if (key == QLatin1String("enemy")) {
        QVector<SolomonEnemy> enemies = next.enemies(room_);
        const int i = parts.value(1).toInt();
        const int type = hexOf(value.toString());
        if (i < 0 || i >= enemies.size() || type < 0) return false;
        enemies[i].type = type;
        next.setEnemies(room_, enemies);
        return commit(next, tr("Edit an enemy in %1").arg(title()), error);
    }
    if (key == QLatin1String("rom_layout")) {
        const bool expanded = value.toString().startsWith(QLatin1String("Expanded"));
        next.setExpanded(expanded);
        const QString overflow = next.overflow();
        if (!overflow.isEmpty()) {
            if (error) *error = overflow;
            return false;
        }
        return session_->edit(expanded ? tr("Expand the ROM") : tr("Return to the original ROM"),
                              [next](SolomonState& s) { s.rooms = next; }, error);
    }
    if (key == QLatin1String("key_status") || key == QLatin1String("time_rate")) {
        QJsonObject metadata = next.metadata(room_);
        int status = metadata.value(QStringLiteral("status_rate_raw")).toInt();
        if (key == QLatin1String("key_status")) {
            const int where = hidings().indexOf(value.toString());
            status = (status & 0x3f) | (where == 1 ? 0x80 : where == 2 ? 0x40 : 0);
        } else {
            status = (status & 0xf0) | std::clamp(value.toInt(), 0, 15);
        }
        metadata.insert(QStringLiteral("status_rate_raw"), status);
        next.setMetadata(room_, metadata);
        return commit(next, key == QLatin1String("key_status") ? tr("Edit the key of %1").arg(title())
                                                               : tr("Edit the timer of %1").arg(title()),
                      error);
    }
    if (key == QLatin1String("enemy_set") || key == QLatin1String("schedule")) {
        const QString mirror = parts.value(1);
        if (!mirror.startsWith(QLatin1String("mirror_"))) return false;
        bool ok = false;
        const int index = value.toString().section(QLatin1Char(' '), 1, 1).toInt(&ok);
        if (!ok) return false;
        QJsonObject metadata = next.metadata(room_);
        metadata.insert(mirror + (key == QLatin1String("enemy_set") ? QStringLiteral("_enemy_set") : QStringLiteral("_schedule")),
                        index);
        next.setMetadata(room_, metadata);
        return commit(next, tr("Edit a mirror of %1").arg(title()), error);
    }
    if (key == QLatin1String("constellation") || key == QLatin1String("room_constellation")) {
        // The item list ends in a plain end opcode or in a constellation;
        // both carry the tileset in bits 2-3.
        QJsonArray commands = next.itemCommands(room_);
        if (commands.isEmpty()) return false;
        QJsonObject last = commands.last().toObject();
        const int tileset = bank();
        const QString wanted = value.toString();
        if (wanted == QLatin1String("None")) {
            QJsonObject end;
            end.insert(QStringLiteral("kind"), QStringLiteral("end"));
            end.insert(QStringLiteral("opcode"), 0xe0 | (tileset << 2));
            last = end;
        } else {
            if (tileset > 2) {
                if (error) *error = tr("Rooms on tileset 3 have no constellation.");
                return false;
            }
            int opcode = -1;
            for (int k = 0; k < 4; ++k) {
                if (constellationName(0xf0 | (tileset << 2) | k) == wanted) opcode = 0xf0 | (tileset << 2) | k;
            }
            if (opcode < 0) return false;
            if (last.value(QStringLiteral("kind")).toString() != QLatin1String("constellation")) {
                last = QJsonObject();
                last.insert(QStringLiteral("kind"), QStringLiteral("constellation"));
                last.insert(QStringLiteral("position"), pointJson(QPoint(6, 1)));
            }
            last.insert(QStringLiteral("opcode"), opcode);
        }
        commands.replace(commands.size() - 1, last);
        next.setItemCommands(room_, commands);
        return commit(next, tr("Change the constellation of %1").arg(title()), error);
    }
    if (key == QLatin1String("spawn_lifetime")) {
        QJsonObject root = next.root();
        QJsonArray list = root.value(QStringLiteral("rooms")).toArray();
        QJsonObject room = list.at(room_).toObject();
        QJsonObject stream = room.value(QStringLiteral("enemy_stream")).toObject();
        stream.insert(QStringLiteral("spawn_lifetime"), std::clamp(value.toInt(), 0, 255));
        room.insert(QStringLiteral("enemy_stream"), stream);
        list.replace(room_, room);
        root.insert(QStringLiteral("rooms"), list);
        return commit(SolomonRooms(root), tr("Edit %1").arg(title()), error);
    }
    if (key == QLatin1String("bank")) {
        // Bits 2-3 of the opcode that ends the item list.
        const int bank = std::clamp(value.toInt(), 0, 3);
        QJsonArray commands = next.itemCommands(room_);
        for (int c = commands.size() - 1; c >= 0; --c) {
            QJsonObject command = commands.at(c).toObject();
            const QString kind = command.value(QStringLiteral("kind")).toString();
            if (kind != QLatin1String("end") && kind != QLatin1String("constellation")) continue;
            int opcode = command.value(QStringLiteral("opcode")).toInt();
            if (kind == QLatin1String("constellation") && bank > 2) {
                if (error) *error = tr("Tileset 3 has no constellations; remove the constellation first.");
                return false;
            }
            if (opcode == 0) opcode = 0xe0;
            command.insert(QStringLiteral("opcode"), (opcode & ~0x0c) | (bank << 2));
            commands.replace(c, command);
            break;
        }
        next.setItemCommands(room_, commands);
        return commit(next, tr("Change the patterns of %1").arg(title()), error);
    }
    return TileMapModel::setField(key, value, error);
}

// ---------------------------------------------------------------- playtest

SolomonPlaytestModel::SolomonPlaytestModel(SolomonSession* session) : EmulatorModel(session), session_(session) {
    connect(session_, &SolomonSession::startChanged, this, [this] {
        emit toolsChanged();
        emit contentChanged();
    });
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::contentChanged);
}

QString SolomonPlaytestModel::coreLabel() const {
    return session_->rooms().revision() == QLatin1String("europe") ? tr("MAME · nespal")
                                                                   : tr("MAME · nes");
}

QString SolomonPlaytestModel::startLabel() const {
    return session_->selectedStart();
}

void SolomonPlaytestModel::play() {
    session_->playSelected();
}

bool SolomonPlaytestModel::isRecording() const {
    return session_->isRecording();
}

sdk::Inspection SolomonPlaytestModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Session");
    inspection.title = startLabel();
    inspection.note = tr("Play from selected builds the ROM with the kit and enters the room open in "
                         "Rooms through the game's own room loader; Play starts from power-on.");
    inspection.engineTag = QStringLiteral("solomon-point-playtest");
    return inspection;
}

}  // namespace rl::solomon
