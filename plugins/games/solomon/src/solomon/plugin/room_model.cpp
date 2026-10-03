#include "solomon/plugin/room_model.hpp"

#include "nes/editors.hpp"
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
    if (key == QLatin1String("door")) return QStringLiteral("Door");
    if (key == QLatin1String("key")) return QStringLiteral("Key");
    if (key == QLatin1String("mirror_1")) return QStringLiteral("Spawn Mirror 1");
    if (key == QLatin1String("mirror_2")) return QStringLiteral("Spawn Mirror 2");
    if (key == QLatin1String("player_start")) return QStringLiteral("Wizard · start");
    return key;
}

const QStringList& hidings() {
    static const QStringList kHidings{QStringLiteral("In the open"), QStringLiteral("Hidden"),
                                      QStringLiteral("In a brown block")};
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
    return {{QStringLiteral("blocks"), QStringLiteral("Blocks")},
            {QStringLiteral("items"), QStringLiteral("Items")},
            {QStringLiteral("enemies"), QStringLiteral("Enemies")}};
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
        return {{QStringLiteral("paint"), QStringLiteral("Paint blocks (right button takes a cell)"), QStringLiteral("paint")},
                {QStringLiteral("pick"), QStringLiteral("Take a cell's blocks as the brush"), QStringLiteral("pick")}};
    }
    return {{QStringLiteral("select"), QStringLiteral("Select and move"), QStringLiteral("select")},
            {QStringLiteral("place"), QStringLiteral("Add the library item"), QStringLiteral("place")},
            {QStringLiteral("remove"), QStringLiteral("Delete"), QStringLiteral("erase")}};
}

QImage SolomonRoomModel::render() const {
    return session_->painter().room(room_, SolomonPainter::Options(), draft_ ? &*draft_ : nullptr);
}

// ---------------------------------------------------------------- blocks

QVector<sdk::CellBrush> SolomonRoomModel::brushes() const {
    if (layer_ != QLatin1String("blocks")) return {};
    return {{0, QStringLiteral("Empty"), QColor()}, {brush_, QStringLiteral("Brush"), QColor()}};
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
        return commit(next, QStringLiteral("Paint blocks in %1").arg(title()), nullptr);
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
    if (next != session_->rooms()) commit(next, QStringLiteral("Paint blocks in %1").arg(title()), nullptr);
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
            item.label = QStringLiteral("Item %1").arg(hex(type));
            item.icon = itemIcon(type);
            items_ << item;
        }
    }
    sdk::Library library;
    library.grid = true;
    library.hint = QStringLiteral("click: pick · drag onto the room");
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
            item.label = QStringLiteral("Enemy %1").arg(hex(type));
            item.icon = icon;
            enemies_ << item;
        }
    }
    sdk::Library library;
    library.grid = true;
    library.hint = QStringLiteral("click: pick · drag onto the room");
    library.items = enemies_;
    return library;
}

sdk::Library SolomonRoomModel::sheet() const {
    if (layer_ == QLatin1String("items")) return itemLibrary();
    if (layer_ == QLatin1String("enemies")) return enemyLibrary();
    sdk::Library library;
    library.grid = true;
    library.hint = QStringLiteral("click: brush · drag onto a cell");
    const SolomonPainter& p = session_->painter();
    const auto add = [&](int value, const QString& label, QImage icon) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("block:%1").arg(value);
        item.label = label;
        item.icon = icon;
        library.items << item;
    };
    add(0, QStringLiteral("Empty"), p.patternIcon(kEmptyPattern, bank(), group()));
    add(1, QStringLiteral("Brown block (breakable)"), p.patternIcon(kBrownPattern, bank(), group()));
    add(2, QStringLiteral("White block (solid)"), p.patternIcon(kWhitePattern, bank(), group()));
    QImage both = p.patternIcon(kBrownPattern, bank(), group());
    {
        QPainter painter(&both);
        painter.setOpacity(0.5);
        painter.drawImage(0, 0, p.patternIcon(kWhitePattern, bank(), group(), true));
    }
    add(3, QStringLiteral("White over brown"), both);
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
                add(QStringLiteral("item:%1").arg(c), QStringLiteral("Item %1").arg(hex(type)),
                    QRect(point(command.value(QStringLiteral("position"))), QSize(1, 1)));
            } else if (kind == QLatin1String("repeat")) {
                const QJsonArray positions = command.value(QStringLiteral("positions")).toArray();
                for (int p = 0; p < positions.size(); ++p) {
                    add(QStringLiteral("repeat:%1:%2").arg(c).arg(p), QStringLiteral("Item %1").arg(hex(type)),
                        QRect(point(positions.at(p)), QSize(1, 1)));
                }
            } else if (kind == QLatin1String("constellation")) {
                add(QStringLiteral("constellation:%1").arg(c), QStringLiteral("Constellation"),
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
            add(QStringLiteral("enemy:%1").arg(i), QStringLiteral("Enemy %1").arg(hex(enemies.at(i).type)),
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
    if (next != session_->rooms()) commit(next, QStringLiteral("Move %1 in %2").arg(objectLabel(selected_), title()), nullptr);
    emit contentChanged();
}

bool SolomonRoomModel::commit(const SolomonRooms& next, const QString& text, QString* error) {
    const int items = next.itemBytes();
    if (items > kItemArenaBytes) {
        if (error) {
            *error = QStringLiteral("The item lists of all rooms share %1 bytes; this needs %2. Remove an item in "
                                    "any room first.").arg(kItemArenaBytes).arg(items);
        }
        return false;
    }
    const int enemies = next.enemyBytes();
    if (enemies > kEnemyArenaBytes) {
        if (error) {
            *error = QStringLiteral("The enemy lists of all rooms share %1 bytes; this needs %2.").arg(kEnemyArenaBytes).arg(enemies);
        }
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
            if (error) *error = QStringLiteral("A room holds at most %1 enemies.").arg(kMaximumEnemies);
            return false;
        }
        enemies << SolomonEnemy{type, cell};
        next.setEnemies(room_, enemies);
        if (!commit(next, QStringLiteral("Add enemy %1 to %2").arg(hex(type), title()), error)) return false;
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
        if (!commit(next, QStringLiteral("Add item %1 to %2").arg(hex(type), title()), error)) return false;
        setCurrentLayer(QStringLiteral("items"));
        selectObject(QStringLiteral("item:%1").arg(at));
        return true;
    }
    if (error) *error = QStringLiteral("Pick something in the library first.");
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
        if (parts.first() == QLatin1String("item")) {
            commands.removeAt(c);
        } else {
            QJsonArray positions = command.value(QStringLiteral("positions")).toArray();
            positions.removeAt(parts.value(2).toInt());
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
                         ? QStringLiteral("The constellation ends the room's item list; move it instead.")
                         : QStringLiteral("%1 stays in the room; move it instead.").arg(objectLabel(id));
        }
        return false;
    }
    selectObject(QString());
    return commit(next, QStringLiteral("Delete %1 from %2").arg(objectLabel(id), title()), error);
}

QString SolomonRoomModel::objectLabel(const QString& id) const {
    for (const sdk::MapObject& o : objects()) {
        if (o.id == id) return o.label;
    }
    return id.startsWith(QLatin1String("meta:")) ? metadataLabel(id.mid(5)) : QStringLiteral("an object");
}

// ---------------------------------------------------------------- inspector

sdk::Budget SolomonRoomModel::budget() const {
    const SolomonRooms& r = rooms();
    sdk::Budget budget;
    budget.used = r.itemBytes();
    budget.capacity = kItemArenaBytes;
    budget.text = QStringLiteral("this room: items %1 B, enemies %2 / %3 · all rooms: items %4 / %5 B, enemies %6 / %7 B")
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
    return QStringLiteral("%1 · CHR bank %2 · palette group %3").arg(title()).arg(bank()).arg(group());
}

sdk::Inspection SolomonRoomModel::inspect() const {
    sdk::Inspection inspection;
    const SolomonRooms& r = rooms();
    const QStringList parts = selected_.split(QLatin1Char(':'));
    if (parts.first() == QLatin1String("item") || parts.first() == QLatin1String("repeat")) {
        const QJsonObject command = r.itemCommands(room_).at(parts.value(1).toInt()).toObject();
        const int type = command.value(QStringLiteral("type")).toInt();
        inspection.kicker = parts.first() == QLatin1String("repeat") ? QStringLiteral("Repeated item") : QStringLiteral("Item");
        inspection.title = QStringLiteral("Item %1").arg(hex(type));
        QStringList choices;
        QVector<QImage> icons;
        for (const sdk::LibraryItem& item : itemLibrary().items) {
            choices << item.label;
            icons << item.icon;
        }
        const int hiding = type >= 0xc0 ? 0 : (type & 0x80) ? 2 : (type & 0x40) ? 1 : 0;
        inspection.fields = {choice(QStringLiteral("type"), QStringLiteral("Item"), QStringLiteral("Item %1").arg(hex(type & 0x3f)),
                                    choices, icons),
                             choice(QStringLiteral("hiding"), QStringLiteral("Where"), hidings().at(hiding), hidings())};
        inspection.note = parts.first() == QLatin1String("repeat")
                              ? QStringLiteral("One of several copies the item list places with one command; they "
                                               "share the item. Delete one and the rest stay.")
                              : QStringLiteral("Hidden items appear when Wizard finds them; an item in a brown block "
                                               "shows when the block breaks.");
        inspection.engineTag = QStringLiteral("solomon.rooms@3");
        inspection.engine = {{QStringLiteral("Type byte"), hex(type)}};
        inspection.library = sdk::Library();
        return inspection;
    }
    if (parts.first() == QLatin1String("enemy")) {
        const QVector<SolomonEnemy> enemies = r.enemies(room_);
        const int i = parts.value(1).toInt();
        if (i >= 0 && i < enemies.size()) {
            inspection.kicker = QStringLiteral("Enemy");
            inspection.title = QStringLiteral("Enemy %1").arg(hex(enemies.at(i).type));
            QStringList choices;
            QVector<QImage> icons;
            for (const sdk::LibraryItem& item : enemyLibrary().items) {
                choices << item.label;
                icons << item.icon;
            }
            inspection.fields = {choice(QStringLiteral("enemy"), QStringLiteral("Type"),
                                        QStringLiteral("Enemy %1").arg(hex(enemies.at(i).type)), choices, icons)};
            inspection.note = QStringLiteral("The low two bits of the type pick the direction it starts in. A room "
                                             "holds at most 17 enemies.");
            inspection.engineTag = QStringLiteral("solomon.rooms@3");
            return inspection;
        }
    }
    if (parts.first() == QLatin1String("meta")) {
        const QString key = parts.value(1);
        inspection.kicker = QStringLiteral("Room fixture");
        inspection.title = metadataLabel(key);
        if (key == QLatin1String("key")) {
            inspection.fields = {integer(QStringLiteral("status_rate_raw"), QStringLiteral("Key state"),
                                         r.metadata(room_).value(QStringLiteral("status_rate_raw")).toInt(), 0, 255,
                                         QStringLiteral("$40+: in a block, $80+: hidden"))};
        }
        inspection.note = key == QLatin1String("player_start")
                              ? QStringLiteral("Where Wizard appears; she faces the room's middle.")
                              : QStringLiteral("Drag it to move it. Every room has one door, one key and its mirrors.");
        inspection.engineTag = QStringLiteral("solomon.rooms@3");
        return inspection;
    }
    if (parts.first() == QLatin1String("constellation")) {
        inspection.kicker = QStringLiteral("Constellation");
        inspection.title = QStringLiteral("Constellation");
        inspection.note = QStringLiteral("The hidden constellation sign; drag it to move it.");
        return inspection;
    }
    inspection.kicker = QStringLiteral("Room");
    inspection.title = title();
    inspection.fields = {choice(QStringLiteral("bank"), QStringLiteral("CHR bank"), QString::number(bank()),
                                {QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")}),
                         integer(QStringLiteral("spawn_lifetime"), QStringLiteral("Spawn lifetime"),
                                 r.spawnLifetime(room_), 0, 255)};
    inspection.note = layer_ == QLatin1String("blocks")
                          ? QStringLiteral("Brown blocks break and can be made by Wizard; white blocks are solid. "
                                           "The right button takes a cell's blocks as the brush.")
                          : QStringLiteral("Drag items and enemies from the sheet onto the room. The door, key, "
                                           "mirrors and Wizard's start are moved, not deleted.");
    inspection.engineTag = QStringLiteral("solomon.rooms@3");
    inspection.engine = {{QStringLiteral("Palette group"), QString::number(group())},
                         {QStringLiteral("Item list"), QStringLiteral("%1 bytes").arg(SolomonRooms::itemStreamBytes(r.itemCommands(room_)))}};
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
            if (error) *error = QStringLiteral("That item cannot be stored that way.");
            return false;
        }
        command.insert(QStringLiteral("type"), type);
        commands.replace(c, command);
        next.setItemCommands(room_, commands);
        return commit(next, QStringLiteral("Edit an item in %1").arg(title()), error);
    }
    if (key == QLatin1String("enemy")) {
        QVector<SolomonEnemy> enemies = next.enemies(room_);
        const int i = parts.value(1).toInt();
        const int type = hexOf(value.toString());
        if (i < 0 || i >= enemies.size() || type < 0) return false;
        enemies[i].type = type;
        next.setEnemies(room_, enemies);
        return commit(next, QStringLiteral("Edit an enemy in %1").arg(title()), error);
    }
    if (key == QLatin1String("status_rate_raw")) {
        QJsonObject metadata = next.metadata(room_);
        metadata.insert(key, std::clamp(value.toInt(), 0, 255));
        next.setMetadata(room_, metadata);
        return commit(next, QStringLiteral("Edit the key of %1").arg(title()), error);
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
        return commit(SolomonRooms(root), QStringLiteral("Edit %1").arg(title()), error);
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
            if (opcode == 0) opcode = 0xe0;
            command.insert(QStringLiteral("opcode"), (opcode & ~0x0c) | (bank << 2));
            commands.replace(c, command);
            break;
        }
        next.setItemCommands(room_, commands);
        return commit(next, QStringLiteral("Change the patterns of %1").arg(title()), error);
    }
    return TileMapModel::setField(key, value, error);
}

// ---------------------------------------------------------------- playtest

SolomonPlaytestModel::SolomonPlaytestModel(SolomonSession* session) : EmulatorModel(session), session_(session) {
    connect(session_, &SolomonSession::startChanged, this, [this] {
        emit toolsChanged();
        emit contentChanged();
    });
}

QString SolomonPlaytestModel::coreLabel() const {
    return session_->rooms().revision() == QLatin1String("europe") ? QStringLiteral("MAME · nespal")
                                                                   : QStringLiteral("MAME · nes");
}

QString SolomonPlaytestModel::startLabel() const {
    return session_->selectedStart();
}

void SolomonPlaytestModel::play() {
    session_->playSelected();
}

sdk::Inspection SolomonPlaytestModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Session");
    inspection.title = startLabel();
    inspection.note = QStringLiteral("Play from selected builds the ROM with the kit and enters the room open in "
                                     "Rooms through the game's own room loader; Play starts from power-on.");
    inspection.engineTag = QStringLiteral("solomon-point-playtest");
    return inspection;
}

}  // namespace rl::solomon
