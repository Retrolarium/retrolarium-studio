#include "doraemon/render/painter.hpp"

#include "nes/palette.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>

namespace rl::doraemon {

QString worldLabel(World world) {
    return QStringLiteral("World %1").arg(int(world));
}

QString hierarchyId(World world) {
    return QStringLiteral("world%1-hierarchy").arg(int(world));
}

QString chrId(World world) {
    return QStringLiteral("chr-bank-%1").arg(int(world) - 1);
}

QStringList paletteIds(const nes::Bundle& bundle, World world) {
    QStringList result;
    for (int p = 0; p < 16; ++p) {
        const QString id = QStringLiteral("world%1-palette-%2").arg(int(world)).arg(p, 2, 10, QLatin1Char('0'));
        if (!bundle.contains(id)) break;
        result << id;
    }
    return result;
}

int areaPreset(const nes::Bundle& bundle, World world, const QString& map, int column, int row) {
    if (world == World::Three) {
        // An 8x8 grid of 256x256 rooms; each names its preset at $ADD2.
        if (!bundle.contains(ids::kRoomSelectors)) return 0;
        const QJsonArray selectors = bundle.resource(ids::kRoomSelectors).value(QStringLiteral("selectors")).toArray();
        return selectors.at((row / 8) * 8 + column / 8).toInt();
    }
    if (map == QLatin1String("city")) return row < 22 ? 1 : 0;
    // The underground rooms (data/world1/underground_rooms.json): bands of
    // rows, each split into regions by the room cameras; presets 3-11 by
    // room, 2 on the finale. Duplicate entrances share their room's preset.
    struct Region {
        int band;
        int start;
        int preset;
    };
    static const Region kRegions[] = {{0, 8, 8},  {8, 0, 2},  {8, 16, 3}, {8, 32, 4},
                                      {8, 48, 5}, {16, 0, 6}, {16, 16, 7}, {16, 24, 10}};
    int band = 0;
    for (const Region& r : kRegions) {
        if (r.band <= row) band = r.band;
    }
    int preset = -1;
    for (const Region& r : kRegions) {
        if (r.band == band && (preset < 0 || r.start <= column)) preset = r.preset;
    }
    return std::max(preset, 0);
}

DoraemonPainter::DoraemonPainter(const nes::Bundle& bundle) : bundle_(&bundle) {
    for (World w : {World::One, World::Two, World::Three}) {
        if (bundle.contains(chrId(w))) chr_.insert(chrId(w), bundle.allPixels(chrId(w)));
        if (w == World::Three && bundle.contains(QStringLiteral("chr-bank-3"))) {
            chr_.insert(QStringLiteral("chr-bank-3"), bundle.allPixels(QStringLiteral("chr-bank-3")));
        }
        if (w != World::Two && bundle.contains(hierarchyId(w))) hierarchies_.insert(int(w), bundle.resource(hierarchyId(w)));
    }
}

QColor DoraemonPainter::color(int index) const {
    return QColor::fromRgb(nes::masterColor(index & 0x3f, "nes"));
}

QVector<int> DoraemonPainter::colours(World world, int preset, int row) const {
    const QStringList presets = paletteIds(*bundle_, world);
    const QVector<int> values = presets.isEmpty() ? QVector<int>()
                                                  : bundle_->tableValues(presets.at(std::clamp(preset, 0, int(presets.size()) - 1)));
    QVector<int> result = values.mid(row * 4, 4);
    while (result.size() < 4) result << 0x0f;
    return result;
}

QImage DoraemonPainter::tile(World world, int tile, const QVector<int>& colours, bool sprite) const {
    const QVector<quint8> pixels = chr_.value(chrId(world));
    // Backgrounds use the bank's second pattern table ($1000), sprites its first.
    const int base = ((sprite ? 0 : 256) + (tile & 0xff)) * 64;
    QImage image(8, 8, QImage::Format_ARGB32);
    for (int i = 0; i < 64; ++i) {
        const int pen = base + i < pixels.size() ? pixels.at(base + i) : 0;
        image.setPixel(i % 8, i / 8, sprite && pen == 0 ? qRgba(0, 0, 0, 0) : color(colours.value(pen)).rgba());
    }
    return image;
}

QImage DoraemonPainter::shellTile(int tile, const QVector<int>& colours, int table) const {
    const QVector<quint8> pixels = chr_.value(QStringLiteral("chr-bank-3"));
    const int base = ((table & 1) * 256 + (tile & 0xff)) * 64;
    QImage image(8, 8, QImage::Format_ARGB32);
    for (int i = 0; i < 64; ++i) {
        const int pen = base + i < pixels.size() ? pixels.at(base + i) : 0;
        image.setPixel(i % 8, i / 8, color(colours.value(pen)).rgba());
    }
    return image;
}

QJsonObject DoraemonPainter::hierarchy(World world) const {
    return hierarchies_.value(int(world));
}

QImage DoraemonPainter::smallBlock(World world, int block, int preset) const {
    const quint64 key = (quint64(1) << 40) | (quint64(world) << 32) | (quint64(preset) << 16) | quint64(block & 0xff);
    const auto found = cache_.constFind(key);
    if (found != cache_.constEnd()) return *found;
    const QJsonObject h = hierarchy(world);
    const QJsonArray tiles = h.value(QStringLiteral("small_blocks")).toArray().at(block).toArray();
    const int palette = h.value(QStringLiteral("attributes")).toArray().at(block).toObject().value(QStringLiteral("palette")).toInt();
    const QVector<int> c = colours(world, preset, palette & 3);
    QImage image(16, 16, QImage::Format_ARGB32);
    QPainter painter(&image);
    for (int q = 0; q < 4; ++q) painter.drawImage((q % 2) * 8, (q / 2) * 8, tile(world, tiles.at(q).toInt(), c));
    painter.end();
    cache_.insert(key, image);
    return image;
}

QImage DoraemonPainter::bigBlock(World world, int block, int preset) const {
    const quint64 key = (quint64(2) << 40) | (quint64(world) << 32) | (quint64(preset) << 16) | quint64(block & 0xff);
    const auto found = cache_.constFind(key);
    if (found != cache_.constEnd()) return *found;
    const QJsonArray smalls = hierarchy(world).value(QStringLiteral("big_blocks")).toArray().at(block).toArray();
    QImage image(32, 32, QImage::Format_ARGB32);
    QPainter painter(&image);
    for (int q = 0; q < 4; ++q) painter.drawImage((q % 2) * 16, (q / 2) * 16, smallBlock(world, smalls.at(q).toInt(), preset));
    painter.end();
    cache_.insert(key, image);
    return image;
}

QImage DoraemonPainter::metatile(int metatile, int preset) const {
    const quint64 key = (quint64(3) << 40) | (quint64(preset) << 16) | quint64(metatile & 0xff);
    const auto found = cache_.constFind(key);
    if (found != cache_.constEnd()) return *found;
    const QJsonObject m = bundle_->contains(ids::kWorld2Metatiles) ? bundle_->resource(ids::kWorld2Metatiles) : QJsonObject();
    const QJsonArray tiles = m.value(QStringLiteral("tiles")).toArray().at(metatile).toArray();
    const QVector<int> c = colours(World::Two, preset, m.value(QStringLiteral("palettes")).toArray().at(metatile).toInt() & 3);
    QImage image(16, 16, QImage::Format_ARGB32);
    QPainter painter(&image);
    for (int q = 0; q < 4; ++q) painter.drawImage((q % 2) * 8, (q / 2) * 8, tile(World::Two, tiles.at(q).toInt(), c));
    painter.end();
    cache_.insert(key, image);
    return image;
}

QVector<int> DoraemonPainter::mapCells(World world, const QString& map, int* width, int* height) const {
    return mapCellsIn(*bundle_, world, map, width, height);
}

QVector<int> mapCellsIn(const nes::Bundle& bundle, World world, const QString& map, int* width, int* height) {
    QVector<int> cells;
    if (!bundle.contains(hierarchyId(world))) return cells;
    for (const QJsonValue& v : bundle.resource(hierarchyId(world)).value(QStringLiteral("maps")).toArray()) {
        const QJsonObject m = v.toObject();
        if (m.value(QStringLiteral("id")).toString() != map) continue;
        if (width) *width = m.value(QStringLiteral("width")).toInt();
        if (height) *height = m.value(QStringLiteral("height")).toInt();
        for (const QJsonValue& row : m.value(QStringLiteral("rows")).toArray()) {
            for (const QJsonValue& cell : row.toArray()) cells << cell.toInt();
        }
    }
    return cells;
}

QImage DoraemonPainter::map(World world, const QString& map) const {
    int width = 0;
    int height = 0;
    const QVector<int> cells = mapCells(world, map, &width, &height);
    QImage image(std::max(1, width) * 32, std::max(1, height) * 32, QImage::Format_ARGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    for (int i = 0; i < cells.size() && width > 0; ++i) {
        const int x = i % width;
        const int y = i / width;
        painter.drawImage(x * 32, y * 32, bigBlock(world, cells.at(i), areaPreset(*bundle_, world, map, x, y)));
    }
    return image;
}

}  // namespace rl::doraemon
