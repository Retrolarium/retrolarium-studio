#include "solomon/data/documents.hpp"

#include "sdk/i18n.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace rl::solomon {

namespace {

[[noreturn]] void fail(const QString& message) { throw SolomonError(message.toStdString()); }

}  // namespace

QJsonObject loadJson(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(rlTr("solomon", "Cannot read %1: %2").arg(path, file.errorString()));
    }
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &error);
    if (!json.isObject()) {
        fail(rlTr("solomon", "%1 is not a JSON document: %2").arg(path, error.errorString()));
    }
    return json.object();
}

void saveJson(const QString& path, const QJsonObject& root) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(rlTr("solomon", "Cannot write %1: %2").arg(path, file.errorString()));
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        fail(rlTr("solomon", "Cannot write %1: %2").arg(path, file.errorString()));
    }
}

QPoint point(const QJsonValue& value) {
    const QJsonObject o = value.toObject();
    return QPoint(o.value(QStringLiteral("x")).toInt(-1), o.value(QStringLiteral("y")).toInt(-1));
}

QJsonObject pointJson(QPoint p) {
    QJsonObject o;
    o.insert(QStringLiteral("x"), p.x());
    o.insert(QStringLiteral("y"), p.y());
    return o;
}

// ---------------------------------------------------------------- rooms

int SolomonRooms::roomCount() const { return root_.value(QStringLiteral("rooms")).toArray().size(); }

QJsonObject SolomonRooms::room(int index) const {
    const QJsonArray rooms = root_.value(QStringLiteral("rooms")).toArray();
    if (index < 0 || index >= rooms.size()) {
        fail(rlTr("solomon", "There is no room %1.").arg(index + 1));
    }
    return rooms.at(index).toObject();
}

void SolomonRooms::setRoom(int index, const QJsonObject& room) {
    QJsonArray rooms = root_.value(QStringLiteral("rooms")).toArray();
    rooms.replace(index, room);
    root_.insert(QStringLiteral("rooms"), rooms);
}

int SolomonRooms::roomNumber(int index) const { return room(index).value(QStringLiteral("number")).toInt(index + 1); }

bool SolomonRooms::block(int index, QPoint cell, bool white) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= kWidth || cell.y() >= kHeight) return false;
    return room(index)
               .value(white ? QStringLiteral("white") : QStringLiteral("brown"))
               .toArray()
               .at(cell.y())
               .toArray()
               .at(cell.x())
               .toInt() != 0;
}

void SolomonRooms::setBlock(int index, QPoint cell, bool white, bool on) {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= kWidth || cell.y() >= kHeight) {
        fail(rlTr("solomon", "The cell is outside the room."));
    }
    QJsonObject r = room(index);
    const QString key = white ? QStringLiteral("white") : QStringLiteral("brown");
    QJsonArray rows = r.value(key).toArray();
    // Rows a hand-edited document leaves short grow to the cell edited.
    while (rows.size() <= cell.y()) rows.append(QJsonArray());
    QJsonArray row = rows.at(cell.y()).toArray();
    while (row.size() <= cell.x()) row.append(0);
    row.replace(cell.x(), on ? 1 : 0);
    rows.replace(cell.y(), row);
    r.insert(key, rows);
    setRoom(index, r);
}

QJsonArray SolomonRooms::itemCommands(int index) const {
    return room(index).value(QStringLiteral("item_stream")).toObject().value(QStringLiteral("commands")).toArray();
}

void SolomonRooms::setItemCommands(int index, const QJsonArray& commands) {
    QJsonObject r = room(index);
    QJsonObject stream = r.value(QStringLiteral("item_stream")).toObject();
    stream.insert(QStringLiteral("commands"), commands);
    r.insert(QStringLiteral("item_stream"), stream);
    setRoom(index, r);
}

QJsonObject SolomonRooms::metadata(int index) const {
    return room(index).value(QStringLiteral("item_stream")).toObject().value(QStringLiteral("metadata")).toObject();
}

void SolomonRooms::setMetadata(int index, const QJsonObject& metadata) {
    QJsonObject r = room(index);
    QJsonObject stream = r.value(QStringLiteral("item_stream")).toObject();
    stream.insert(QStringLiteral("metadata"), metadata);
    r.insert(QStringLiteral("item_stream"), stream);
    setRoom(index, r);
}

QPoint SolomonRooms::metadataPosition(int index, const QString& key) const {
    const QPoint p = point(metadata(index).value(key));
    return p.x() >= 0 && p.y() >= 0 && p.x() < kWidth && p.y() < kHeight ? p : QPoint(-1, -1);
}

int SolomonRooms::spawnLifetime(int index) const {
    return room(index).value(QStringLiteral("enemy_stream")).toObject().value(QStringLiteral("spawn_lifetime")).toInt();
}

QVector<SolomonEnemy> SolomonRooms::enemies(int index) const {
    QVector<SolomonEnemy> result;
    for (const QJsonValue& value :
         room(index).value(QStringLiteral("enemy_stream")).toObject().value(QStringLiteral("enemies")).toArray()) {
        const QJsonObject e = value.toObject();
        result << SolomonEnemy{e.value(QStringLiteral("type")).toInt(), point(e.value(QStringLiteral("position")))};
    }
    return result;
}

void SolomonRooms::setEnemies(int index, const QVector<SolomonEnemy>& enemies) {
    if (enemies.size() > kMaximumEnemies) {
        fail(rlTr("solomon", "A room holds at most %1 enemies; the game keeps them in a fixed pool.")
                 .arg(kMaximumEnemies));
    }
    QJsonArray list;
    for (const SolomonEnemy& e : enemies) {
        if (e.type < 0x18 || e.type > 0x83) {
            fail(rlTr("solomon", "Enemy types run from $18 to $83."));
        }
        QJsonObject json;
        json.insert(QStringLiteral("position"), pointJson(e.position));
        json.insert(QStringLiteral("type"), e.type);
        list.append(json);
    }
    QJsonObject r = room(index);
    QJsonObject stream = r.value(QStringLiteral("enemy_stream")).toObject();
    stream.insert(QStringLiteral("enemies"), list);
    r.insert(QStringLiteral("enemy_stream"), stream);
    setRoom(index, r);
}

int SolomonRooms::chrBank(const QJsonArray& commands) {
    for (int i = commands.size() - 1; i >= 0; --i) {
        const QJsonObject command = commands.at(i).toObject();
        const QString kind = command.value(QStringLiteral("kind")).toString();
        if (kind != QLatin1String("end") && kind != QLatin1String("constellation")) continue;
        const int opcode = command.value(QStringLiteral("opcode")).toInt(-1);
        if (opcode == 0 || (opcode >= 0xe0 && opcode <= 0xfb)) return (opcode >> 2) & 3;
        break;
    }
    return 0;
}

int SolomonRooms::itemStreamBytes(const QJsonArray& commands) {
    int bytes = 10;  // mirror tables, status rate, five positions
    for (const QJsonValue& value : commands) {
        const QJsonObject command = value.toObject();
        const QString kind = command.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("end")) bytes += 1;
        else if (kind == QLatin1String("repeat"))
            bytes += 2 + command.value(QStringLiteral("positions")).toArray().size();
        else bytes += 2;
    }
    return bytes;
}

int SolomonRooms::itemBytes() const {
    int bytes = 0;
    for (int r = 0; r < roomCount(); ++r) bytes += itemStreamBytes(itemCommands(r));
    return bytes;
}

QVector<SolomonMirrorSet> SolomonRooms::mirrorSets() const {
    QVector<SolomonMirrorSet> result;
    for (const QJsonValue& value : root_.value(QStringLiteral("mirror_enemy_sets")).toArray()) {
        const QJsonObject o = value.toObject();
        SolomonMirrorSet set;
        for (const QJsonValue& type : o.value(QStringLiteral("enemy_types")).toArray()) set.types << type.toInt();
        set.loopOffset = o.value(QStringLiteral("loop_offset")).toInt();
        result << set;
    }
    return result;
}

void SolomonRooms::setMirrorSet(int index, const SolomonMirrorSet& set) {
    if (set.types.isEmpty()) fail(rlTr("solomon", "A mirror enemy set needs at least one enemy."));
    for (int type : set.types) {
        if (type < 0x18 || type > 0x83) fail(rlTr("solomon", "Enemy types run from $18 to $83."));
    }
    if (set.loopOffset < 0 || set.loopOffset >= set.types.size()) {
        fail(rlTr("solomon", "The loop must start at one of the set's enemies."));
    }
    QJsonArray sets = root_.value(QStringLiteral("mirror_enemy_sets")).toArray();
    if (index < 0 || index >= sets.size()) fail(rlTr("solomon", "There is no mirror enemy set %1.").arg(index));
    QJsonObject o;
    QJsonArray types;
    for (int type : set.types) types.append(type);
    o.insert(QStringLiteral("enemy_types"), types);
    o.insert(QStringLiteral("loop_offset"), set.loopOffset);
    sets.replace(index, o);
    root_.insert(QStringLiteral("mirror_enemy_sets"), sets);
}

int SolomonRooms::mirrorSetBytes() const {
    int bytes = 0;
    for (const SolomonMirrorSet& set : mirrorSets()) bytes += set.types.size() + 1;
    return bytes;
}

QVector<SolomonSchedule> SolomonRooms::schedules() const {
    QVector<SolomonSchedule> result;
    for (const QJsonValue& value : root_.value(QStringLiteral("mirror_schedules")).toArray()) {
        const QJsonObject o = value.toObject();
        SolomonSchedule schedule;
        for (const QJsonValue& v : o.value(QStringLiteral("initial_phase")).toArray()) schedule.initial << v.toInt();
        for (const QJsonValue& v : o.value(QStringLiteral("loop_phase")).toArray()) schedule.loop << v.toInt();
        result << schedule;
    }
    return result;
}

void SolomonRooms::setSchedule(int index, const SolomonSchedule& schedule) {
    if (schedule.initial.size() != 4 || schedule.loop.size() != 4) {
        fail(rlTr("solomon", "A drop schedule has four initial and four looping bytes."));
    }
    QJsonArray list = root_.value(QStringLiteral("mirror_schedules")).toArray();
    if (index < 0 || index >= list.size()) fail(rlTr("solomon", "There is no drop schedule %1.").arg(index));
    QJsonObject o;
    QJsonArray initial;
    QJsonArray loop;
    for (int v : schedule.initial) initial.append(v & 0xff);
    for (int v : schedule.loop) loop.append(v & 0xff);
    o.insert(QStringLiteral("initial_phase"), initial);
    o.insert(QStringLiteral("loop_phase"), loop);
    list.replace(index, o);
    root_.insert(QStringLiteral("mirror_schedules"), list);
}

int expandedDataBytes(const QString& revision) {
    // Bank 0's two stream regions: the old enemy, block and item areas and
    // the audio area, which bank 0 does not need.
    return revision == QLatin1String("europe") ? 3270 + 5620 : 3270 + 5492;
}

void SolomonRooms::setExpanded(bool expanded) {
    if (expanded) root_.insert(QStringLiteral("expanded_rom"), true);
    else root_.remove(QStringLiteral("expanded_rom"));
}

QString SolomonRooms::overflow(bool enemiesFirst) const {
    // The enemy sets the mirrors drop have their own area, smaller in the
    // original layout: switching back to it must not leave them over.
    // The studio builds the original layout only for now; the expanded one
    // (the kit's mapper-66 build) comes back with the next release.
    if (mirrorSetBytes() > mirrorSetCapacity()) {
        return rlTr("solomon", "The mirror enemy sets share %1 bytes; they need %2. Shorten a set.")
            .arg(mirrorSetCapacity())
            .arg(mirrorSetBytes());
    }
    if (!expanded()) {
        if (enemiesFirst && enemyBytes() > kEnemyArenaBytes) {
            return rlTr("solomon",
                        "The enemy lists of all rooms share %1 bytes; they need %2. Remove an enemy in any room.")
                .arg(kEnemyArenaBytes)
                .arg(enemyBytes());
        }
        if (itemBytes() > kItemArenaBytes) {
            return rlTr("solomon",
                        "The item lists of all rooms share %1 bytes; they need %2. Remove an item in any room.")
                .arg(kItemArenaBytes)
                .arg(itemBytes());
        }
        if (enemyBytes() > kEnemyArenaBytes) {
            return rlTr("solomon",
                        "The enemy lists of all rooms share %1 bytes; they need %2. Remove an enemy in any room.")
                .arg(kEnemyArenaBytes)
                .arg(enemyBytes());
        }
        return {};
    }
    for (int r = 0; r < roomCount(); ++r) {
        if (itemStreamBytes(itemCommands(r)) > kExpandedStreamLimit) {
            return rlTr("solomon", "Room %1's item list needs %2 bytes; the game reads at most %3 per room.")
                .arg(r + 1)
                .arg(itemStreamBytes(itemCommands(r)))
                .arg(kExpandedStreamLimit);
        }
    }
    const int total = itemBytes() + enemyBytes();
    if (total > expandedDataBytes(revision())) {
        return rlTr("solomon", "The expanded ROM keeps %1 bytes for every room's items and enemies; they need %2.")
            .arg(expandedDataBytes(revision()))
            .arg(total);
    }
    return {};
}

int SolomonRooms::enemyBytes() const {
    int bytes = 0;
    for (int r = 0; r < roomCount(); ++r) bytes += 2 + 2 * enemies(r).size();
    return bytes;
}

// ---------------------------------------------------------------- art and animations

SolomonPattern pattern(const QJsonObject& art, int index) {
    SolomonPattern result;
    const QJsonObject record = art.value(QStringLiteral("room_map_patterns")).toArray().at(index).toObject();
    result.palette = record.value(QStringLiteral("palette")).toInt() & 3;
    result.tiles[0] = record.value(QStringLiteral("top_left")).toInt();
    result.tiles[1] = record.value(QStringLiteral("top_right")).toInt();
    result.tiles[2] = record.value(QStringLiteral("bottom_left")).toInt();
    result.tiles[3] = record.value(QStringLiteral("bottom_right")).toInt();
    return result;
}

int patternCount(const QJsonObject& art) { return art.value(QStringLiteral("room_map_patterns")).toArray().size(); }

int groupColor(const QJsonObject& art, int group) {
    const QJsonValue value = art.value(QStringLiteral("room_group_colors")).toArray().at(group);
    return value.isObject() ? value.toObject().value(QStringLiteral("value")).toInt() : value.toInt();
}

namespace {

QJsonObject frameForAction(const QJsonObject& animations, int type, int action) {
    const QJsonArray pointers = animations.value(QStringLiteral("group_pointers")).toArray();
    const int group = type >> 2;
    if (group < 0 || group >= pointers.size()) return {};
    const int address = pointers.at(group).toInt() + action * 4;
    QJsonObject descriptor;
    for (const QJsonValue& value : animations.value(QStringLiteral("descriptors")).toArray()) {
        if (value.toObject().value(QStringLiteral("cpu_address")).toInt() == address) {
            descriptor = value.toObject();
            break;
        }
    }
    if (descriptor.isEmpty()) return {};
    int pointer = descriptor.value(QStringLiteral("data_pointer")).toInt();
    if (descriptor.value(QStringLiteral("uses_variants")).toBool()) {
        for (const QJsonValue& value : animations.value(QStringLiteral("selectors")).toArray()) {
            const QJsonObject selector = value.toObject();
            if (selector.value(QStringLiteral("cpu_address")).toInt() == pointer) {
                pointer = selector.value(QStringLiteral("pointers")).toArray().at(type & 3).toInt();
                break;
            }
        }
    }
    // The first frame the animation shows: one phase advance from its
    // initial phase, as the driver steps it.
    int phase = descriptor.value(QStringLiteral("initial_phase")).toInt();
    const int sum = phase + 0xf0;
    phase = sum & 255;
    if (sum <= 255) phase = (phase & 15) * 0x11;
    const int frameAddress = pointer + (phase >> 3) + (phase >> 4);
    for (const QJsonValue& value : animations.value(QStringLiteral("frames")).toArray()) {
        if (value.toObject().value(QStringLiteral("cpu_address")).toInt() == frameAddress) {
            return value.toObject();
        }
    }
    return {};
}

}  // namespace

QJsonObject enemyFrame(const QJsonObject& animations, int type) {
    if (type < 0x18 || type > 0x83) return {};
    const QJsonArray configuration = animations.value(QStringLiteral("enemy_configuration")).toArray();
    const int flags = configuration.at((type - 0x18) >> 2).toInt();
    int action = (type & 3) | ((flags & 8) ? 0 : 0x18);
    // A fairy's first action is the sparkle she appears in; show her as she
    // is once she has appeared.
    if (type >= 0x1c && type <= 0x1f) action = (type & 3) | 0x18;
    return frameForAction(animations, type, action);
}

QJsonObject playerFrame(const QJsonObject& animations, bool facesLeft) {
    return frameForAction(animations, 0, facesLeft ? 0x17 : 0x16);
}

}  // namespace rl::solomon
