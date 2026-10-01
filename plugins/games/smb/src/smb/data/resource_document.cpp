#include "smb/data/resource_document.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace rl::smb {

namespace {

[[noreturn]] void fail(const QString& message) {
    throw SmbError(message.toStdString());
}

constexpr int kTilePixels = 64;

}  // namespace

int SmbArea::areaBytes() const {
    return 2 + 2 * int(objects.size()) + 1;
}

int SmbArea::enemyBytes() const {
    int bytes = 1;
    for (const SmbEnemyRecord& enemy : enemies) {
        bytes += enemy.kind == QLatin1String("entrance") ? 3 : 2;
    }
    return bytes;
}

bool SmbArea::areaFits() const {
    const int used = areaBytes();
    return areaImplicitTerminator ? used == areaCapacity + 1 : used <= areaCapacity;
}

bool SmbArea::enemiesFit() const {
    const int used = enemyBytes();
    return enemyImplicitTerminator ? used == enemyCapacity + 1 : used <= enemyCapacity;
}

SmbResourceDocument SmbResourceDocument::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
    }
    QJsonParseError error{};
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) {
        fail(QStringLiteral("%1 is not valid JSON: %2").arg(path, error.errorString()));
    }
    SmbResourceDocument document;
    document.root_ = json.object();
    if (document.root_.value(QStringLiteral("schema")).toString() != QLatin1String("retrolarium.nes-resource-bundle") ||
        document.root_.value(QStringLiteral("schema_version")).toInt() != 1) {
        fail(QStringLiteral("%1 is not a Retrolarium NES resource bundle").arg(path));
    }
    if (document.root_.value(QStringLiteral("resources")).toArray().isEmpty()) {
        fail(QStringLiteral("%1 has no resources").arg(path));
    }
    return document;
}

void SmbResourceDocument::save(const QString& path) const {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("Cannot write %1: %2").arg(path, file.errorString()));
    }
    // The same layout the kit and the original app write: four-space indents,
    // keys in order.
    file.write(QJsonDocument(root_).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        fail(QStringLiteral("Cannot write %1: %2").arg(path, file.errorString()));
    }
}

QString SmbResourceDocument::game() const {
    return root_.value(QStringLiteral("game")).toString();
}

QString SmbResourceDocument::revision() const {
    return root_.value(QStringLiteral("revision")).toString();
}

int SmbResourceDocument::indexOf(const QString& id) const {
    const QJsonArray resources = root_.value(QStringLiteral("resources")).toArray();
    for (int i = 0; i < resources.size(); ++i) {
        if (resources.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
            return i;
        }
    }
    return -1;
}

QStringList SmbResourceDocument::resourceIds(const QString& codec) const {
    QStringList ids;
    for (const QJsonValue& value : root_.value(QStringLiteral("resources")).toArray()) {
        const QJsonObject resource = value.toObject();
        if (resource.value(QStringLiteral("codec")).toString() == codec) {
            ids << resource.value(QStringLiteral("id")).toString();
        }
    }
    return ids;
}

QJsonObject SmbResourceDocument::resource(const QString& id) const {
    const int index = indexOf(id);
    if (index < 0) {
        fail(QStringLiteral("The project has no resource %1").arg(id));
    }
    return root_.value(QStringLiteral("resources")).toArray().at(index).toObject();
}

void SmbResourceDocument::setResource(const QString& id, const QJsonObject& resource) {
    const int index = indexOf(id);
    if (index < 0) {
        fail(QStringLiteral("The project has no resource %1").arg(id));
    }
    QJsonArray resources = root_.value(QStringLiteral("resources")).toArray();
    resources.replace(index, resource);
    root_.insert(QStringLiteral("resources"), resources);
    if (id == tilesId_) {
        tilesId_.clear();
    }
}

// ---------------------------------------------------------------- levels

QStringList SmbResourceDocument::areaNames(const QString& levels) const {
    QStringList names;
    for (const QJsonValue& value : resource(levels).value(QStringLiteral("areas")).toArray()) {
        names << value.toObject().value(QStringLiteral("name")).toString();
    }
    return names;
}

QString SmbResourceDocument::rendering(const QString& levels) const {
    return resource(levels).value(QStringLiteral("rendering")).toString(QStringLiteral("smb1"));
}

QStringList SmbResourceDocument::areaTypes(const QString& levels) const {
    QStringList types;
    for (const QJsonValue& value : resource(levels).value(QStringLiteral("area_types")).toArray()) {
        types << value.toString();
    }
    return types;
}

SmbArea SmbResourceDocument::area(const QString& levels, const QString& name) const {
    for (const QJsonValue& value : resource(levels).value(QStringLiteral("areas")).toArray()) {
        const QJsonObject a = value.toObject();
        if (a.value(QStringLiteral("name")).toString() != name) {
            continue;
        }
        SmbArea area;
        area.name = name;
        area.areaCapacity = a.value(QStringLiteral("area_capacity")).toInt();
        area.enemyCapacity = a.value(QStringLiteral("enemy_capacity")).toInt();
        area.areaImplicitTerminator = a.value(QStringLiteral("area_implicit_terminator")).toBool();
        area.enemyImplicitTerminator = a.value(QStringLiteral("enemy_implicit_terminator")).toBool();
        const QJsonObject h = a.value(QStringLiteral("header")).toObject();
        area.header = {h.value(QStringLiteral("timer_setting")).toInt(),
                       h.value(QStringLiteral("entrance_control")).toInt(),
                       h.value(QStringLiteral("foreground_or_color")).toInt(),
                       h.value(QStringLiteral("area_style")).toInt(),
                       h.value(QStringLiteral("background_scenery")).toInt(),
                       h.value(QStringLiteral("terrain_control")).toInt()};
        for (const QJsonValue& item : a.value(QStringLiteral("objects")).toArray()) {
            const QJsonObject o = item.toObject();
            area.objects.push_back({o.value(QStringLiteral("column")).toInt(), o.value(QStringLiteral("row")).toInt(),
                                    o.value(QStringLiteral("page_advance")).toBool(),
                                    o.value(QStringLiteral("object_control")).toInt()});
        }
        for (const QJsonValue& item : a.value(QStringLiteral("enemies")).toArray()) {
            const QJsonObject e = item.toObject();
            area.enemies.push_back({e.value(QStringLiteral("kind")).toString(), e.value(QStringLiteral("column")).toInt(),
                                    e.value(QStringLiteral("row")).toInt(),
                                    e.value(QStringLiteral("page_advance")).toBool(),
                                    e.value(QStringLiteral("hard_mode")).toBool(),
                                    e.value(QStringLiteral("object_or_page")).toInt(),
                                    e.value(QStringLiteral("destination_world")).toInt(-1),
                                    e.value(QStringLiteral("destination_page")).toInt(-1)});
        }
        return area;
    }
    fail(QStringLiteral("The levels have no area %1").arg(name));
}

void SmbResourceDocument::setArea(const QString& levels, const SmbArea& area) {
    QJsonObject resource = this->resource(levels);
    QJsonArray areas = resource.value(QStringLiteral("areas")).toArray();
    for (int i = 0; i < areas.size(); ++i) {
        QJsonObject a = areas.at(i).toObject();
        if (a.value(QStringLiteral("name")).toString() != area.name) {
            continue;
        }
        if (area.header.size() != 6) {
            fail(QStringLiteral("An SMB area header has six fields"));
        }
        a.insert(QStringLiteral("header"),
                 QJsonObject{{QStringLiteral("timer_setting"), area.header.at(0)},
                             {QStringLiteral("entrance_control"), area.header.at(1)},
                             {QStringLiteral("foreground_or_color"), area.header.at(2)},
                             {QStringLiteral("area_style"), area.header.at(3)},
                             {QStringLiteral("background_scenery"), area.header.at(4)},
                             {QStringLiteral("terrain_control"), area.header.at(5)}});
        QJsonArray objects;
        for (const SmbAreaObject& o : area.objects) {
            objects.append(QJsonObject{{QStringLiteral("column"), o.column},
                                       {QStringLiteral("row"), o.row},
                                       {QStringLiteral("object_control"), o.control},
                                       {QStringLiteral("page_advance"), o.pageAdvance}});
        }
        a.insert(QStringLiteral("objects"), objects);
        QJsonArray enemies;
        for (const SmbEnemyRecord& e : area.enemies) {
            QJsonObject record{{QStringLiteral("kind"), e.kind},
                               {QStringLiteral("column"), e.column},
                               {QStringLiteral("row"), e.row},
                               {QStringLiteral("object_or_page"), e.objectOrPage},
                               {QStringLiteral("page_advance"), e.pageAdvance},
                               {QStringLiteral("hard_mode"), e.hardMode}};
            if (e.kind == QLatin1String("entrance")) {
                record.insert(QStringLiteral("destination_world"), e.destinationWorld);
                record.insert(QStringLiteral("destination_page"), e.destinationPage);
            }
            enemies.append(record);
        }
        a.insert(QStringLiteral("enemies"), enemies);
        areas.replace(i, a);
        resource.insert(QStringLiteral("areas"), areas);
        setResource(levels, resource);
        return;
    }
    fail(QStringLiteral("The levels have no area %1").arg(area.name));
}

// ---------------------------------------------------------------- world

int SmbResourceDocument::worldCount(const QString& world) const {
    return resource(world).value(QStringLiteral("worlds")).toArray().size();
}

QVector<SmbRoute> SmbResourceDocument::routes(const QString& world, int number) const {
    QVector<SmbRoute> result;
    for (const QJsonValue& value : resource(world).value(QStringLiteral("worlds")).toArray()) {
        const QJsonObject w = value.toObject();
        if (w.value(QStringLiteral("world")).toInt() != number) {
            continue;
        }
        for (const QJsonValue& step : w.value(QStringLiteral("areas")).toArray()) {
            const QJsonObject s = step.toObject();
            result.push_back({s.value(QStringLiteral("area_type")).toInt(), s.value(QStringLiteral("area_index")).toInt(),
                              s.value(QStringLiteral("alternate_bit")).toBool()});
        }
    }
    return result;
}

// ---------------------------------------------------------------- graphics

void SmbResourceDocument::decodeTiles(const QString& chr) const {
    if (tilesId_ == chr) {
        return;
    }
    const QJsonArray tiles = resource(chr).value(QStringLiteral("tiles")).toArray();
    tiles_.fill(0, tiles.size() * kTilePixels);
    for (int t = 0; t < tiles.size(); ++t) {
        const QJsonArray rows = tiles.at(t).toArray();
        for (int y = 0; y < std::min(8, int(rows.size())); ++y) {
            const QJsonArray row = rows.at(y).toArray();
            for (int x = 0; x < std::min(8, int(row.size())); ++x) {
                tiles_[t * kTilePixels + y * 8 + x] = quint8(row.at(x).toInt() & 3);
            }
        }
    }
    tilesId_ = chr;
}

int SmbResourceDocument::tileCount(const QString& chr) const {
    decodeTiles(chr);
    return tiles_.size() / kTilePixels;
}

int SmbResourceDocument::pixel(const QString& chr, int tile, int x, int y) const {
    decodeTiles(chr);
    if (tile < 0 || tile * kTilePixels >= tiles_.size() || x < 0 || x > 7 || y < 0 || y > 7) {
        return 0;
    }
    return tiles_.at(tile * kTilePixels + y * 8 + x);
}

QVector<quint8> SmbResourceDocument::tilePixels(const QString& chr, int tile) const {
    decodeTiles(chr);
    if (tile < 0 || tile * kTilePixels >= tiles_.size()) {
        return QVector<quint8>(kTilePixels, 0);
    }
    return tiles_.mid(tile * kTilePixels, kTilePixels);
}

void SmbResourceDocument::setTilePixels(const QString& chr, int tile, const QVector<quint8>& pixels) {
    if (pixels.size() != kTilePixels) {
        fail(QStringLiteral("A pattern tile has 64 pixels"));
    }
    QJsonObject r = resource(chr);
    QJsonArray tiles = r.value(QStringLiteral("tiles")).toArray();
    if (tile < 0 || tile >= tiles.size()) {
        fail(QStringLiteral("Tile %1 is outside the pattern table").arg(tile));
    }
    QJsonArray rows;
    for (int y = 0; y < 8; ++y) {
        QJsonArray row;
        for (int x = 0; x < 8; ++x) {
            row.append(int(pixels.at(y * 8 + x) & 3));
        }
        rows.append(row);
    }
    tiles.replace(tile, rows);
    r.insert(QStringLiteral("tiles"), tiles);
    setResource(chr, r);
}

std::array<int, 4> SmbResourceDocument::metatileTiles(const QString& metatiles, int encoded) const {
    const QJsonObject r = resource(metatiles);
    const QJsonArray sizes = r.value(QStringLiteral("group_sizes")).toArray();
    const int tileBase = r.value(QStringLiteral("tile_base")).toInt();
    const int group = (encoded >> 6) & 3;
    const int within = encoded & 0x3f;
    std::array<int, 4> stored{0x24, 0x24, 0x24, 0x24};  // the blank tile, as the game falls back
    if (group < sizes.size() && within < sizes.at(group).toInt()) {
        int index = within;
        for (int g = 0; g < group; ++g) {
            index += sizes.at(g).toInt();
        }
        const QJsonArray record = r.value(QStringLiteral("records")).toArray().at(index).toArray();
        for (int i = 0; i < 4 && i < record.size(); ++i) {
            stored[i] = record.at(i).toInt();
        }
    }
    // Stored top left, bottom left, top right, bottom right.
    return {tileBase + stored[0], tileBase + stored[2], tileBase + stored[1], tileBase + stored[3]};
}

// ---------------------------------------------------------------- palettes

QStringList SmbResourceDocument::packetNames(const QString& palettes) const {
    QStringList names;
    for (const QJsonValue& value : resource(palettes).value(QStringLiteral("packets")).toArray()) {
        names << value.toObject().value(QStringLiteral("name")).toString();
    }
    return names;
}

QVector<int> SmbResourceDocument::packetValues(const QString& palettes, const QString& name) const {
    QVector<int> values;
    for (const QJsonValue& value : resource(palettes).value(QStringLiteral("packets")).toArray()) {
        const QJsonObject packet = value.toObject();
        if (packet.value(QStringLiteral("name")).toString() == name) {
            for (const QJsonValue& v : packet.value(QStringLiteral("values")).toArray()) {
                values << v.toInt();
            }
            break;
        }
    }
    return values;
}

void SmbResourceDocument::setPacketValue(const QString& palettes, const QString& name, int entry, int color) {
    if (color < 0 || color > 0x3f) {
        fail(QStringLiteral("NES colours are 00-3F"));
    }
    QJsonObject r = resource(palettes);
    QJsonArray packets = r.value(QStringLiteral("packets")).toArray();
    for (int i = 0; i < packets.size(); ++i) {
        QJsonObject packet = packets.at(i).toObject();
        if (packet.value(QStringLiteral("name")).toString() != name) {
            continue;
        }
        QJsonArray values = packet.value(QStringLiteral("values")).toArray();
        if (entry < 0 || entry >= values.size()) {
            fail(QStringLiteral("Palette %1 has no entry %2").arg(name).arg(entry));
        }
        values.replace(entry, color);
        packet.insert(QStringLiteral("values"), values);
        packets.replace(i, packet);
        r.insert(QStringLiteral("packets"), packets);
        setResource(palettes, r);
        return;
    }
    fail(QStringLiteral("There is no palette %1").arg(name));
}

int SmbResourceDocument::dayBackground(const QString& palettes) const {
    return resource(palettes).value(QStringLiteral("day_background")).toInt();
}

int SmbResourceDocument::nightBackground(const QString& palettes) const {
    return resource(palettes).value(QStringLiteral("night_background")).toInt();
}

QString SmbResourceDocument::paletteModel(const QString& palettes) const {
    return resource(palettes).value(QStringLiteral("palette_model")).toString(QStringLiteral("nes"));
}

QVector<int> SmbResourceDocument::tableValues(const QString& table) const {
    QVector<int> values;
    for (const QJsonValue& v : resource(table).value(QStringLiteral("values")).toArray()) {
        values << v.toInt();
    }
    return values;
}

// ---------------------------------------------------------------- player frames

void SmbResourceDocument::setTableValue(const QString& table, int entry, int color) {
    if (color < 0 || color > 0x3f) {
        fail(QStringLiteral("NES colours are 00-3F"));
    }
    QJsonObject r = resource(table);
    QJsonArray values = r.value(QStringLiteral("values")).toArray();
    if (entry < 0 || entry >= values.size()) {
        fail(QStringLiteral("The palette table has no entry %1").arg(entry));
    }
    values.replace(entry, color);
    r.insert(QStringLiteral("values"), values);
    setResource(table, r);
}

int SmbResourceDocument::playerFrameCount(const QString& frames) const {
    return resource(frames).value(QStringLiteral("frames")).toArray().size();
}

QVector<int> SmbResourceDocument::playerFrameTiles(const QString& frames, int frame) const {
    QVector<int> tiles;
    for (const QJsonValue& v : resource(frames).value(QStringLiteral("frames")).toArray().at(frame).toArray()) {
        tiles << v.toInt();
    }
    return tiles;
}

int SmbResourceDocument::playerTileBase(const QString& frames) const {
    return resource(frames).value(QStringLiteral("tile_base")).toInt();
}

int SmbResourceDocument::playerBlankTile(const QString& frames) const {
    return resource(frames).value(QStringLiteral("blank_tile")).toInt();
}

}  // namespace rl::smb
