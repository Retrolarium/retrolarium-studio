#include "pacman/render/painter.hpp"

#include "nes/palette.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QPainter>
#include <QSet>

#include <algorithm>
#include <deque>

namespace rl::pacman {

namespace maze {

QString tileName(int tile) {
    switch (tile) {
    case 0x00: return rlTr("pacman", "empty floor");
    case 0x01: return rlTr("pacman", "power pellet");
    case 0x02: return rlTr("pacman", "hidden power pellet");
    case 0x03: return QStringLiteral("pellet");
    case 0x04: return rlTr("pacman", "left tunnel edge");
    case 0x05: return rlTr("pacman", "right tunnel edge");
    case 0x06: return rlTr("pacman", "tunnel floor");
    case 0x07: return rlTr("pacman", "cleared floor");
    case 0x08: return rlTr("pacman", "special floor");
    case 0x09: return rlTr("pacman", "alternate pellet");
    case 0x20: return QStringLiteral("space");
    case 0x2c: return rlTr("pacman", "ghost-house door");
    case 0x2d: return rlTr("pacman", "maze blank");
    default: return tile < 0x10 ? QStringLiteral("floor") : QStringLiteral("wall");
    }
}

Report inspect(const QVector<int>& grid) {
    // scripts/authoring/maze_studio_model.py and the kit's pacman_maze.
    Report report;
    if (grid.size() != kRows * kColumns) {
        report.errors << rlTr("pacman", "The maze is not 27x22.");
        return report;
    }
    const auto at = [&grid](int row, int column) { return grid.at(row * kColumns + column); };
    for (int tile : grid) {
        if (tile == 0x01 || tile == 0x03 || tile == 0x09) ++report.pellets;
        if (tile == 0x01) ++report.powerPellets;
    }
    if (report.pellets != 192) {
        report.errors << rlTr("pacman", "%1 pellets; the round ends after 192.").arg(report.pellets);
    }
    if (report.powerPellets != 4) {
        report.errors << rlTr("pacman", "%1 power pellets; the blinking table expects 4.").arg(report.powerPellets);
    }
    if (at(kDoorRow, kDoorColumn) != 0x2c) report.errors << rlTr("pacman", "The ghost-house door at 11,11 must stay $2C.");
    const int spawns[5][2] = {{kPacmanRow, kPacmanColumn}, {10, 11}, {13, 10}, {13, 11}, {13, 12}};
    for (const auto& spawn : spawns) {
        if (at(spawn[0], spawn[1]) >= 0x10) {
            report.errors << rlTr("pacman", "The spawn at %1,%2 is blocked.").arg(spawn[0]).arg(spawn[1]);
        }
    }
    for (int column : {0, kColumns - 1}) {
        if (at(kTunnelRow, column) >= 0x10) {
            report.errors << rlTr("pacman", "The tunnel end at %1,%2 must stay open.").arg(kTunnelRow).arg(column);
        }
    }
    // Passable regions, the tunnel wrapping around.
    QVector<int> sizes;
    QSet<int> remaining;
    for (int i = 0; i < grid.size(); ++i) {
        if (grid.at(i) < 0x10) remaining.insert(i);
    }
    while (!remaining.isEmpty()) {
        const int start = *remaining.begin();
        remaining.remove(start);
        std::deque<int> queue{start};
        int size = 1;
        while (!queue.empty()) {
            const int cell = queue.front();
            queue.pop_front();
            const int row = cell / kColumns;
            const int column = cell % kColumns;
            const int neighbours[4] = {row > 0 ? cell - kColumns : -1, row < kRows - 1 ? cell + kColumns : -1,
                                       row * kColumns + (column + kColumns - 1) % kColumns,
                                       row * kColumns + (column + 1) % kColumns};
            for (int n : neighbours) {
                if (n >= 0 && remaining.remove(n)) {
                    queue.push_back(n);
                    ++size;
                }
            }
        }
        sizes << size;
    }
    std::sort(sizes.begin(), sizes.end(), std::greater<int>());
    if (sizes.size() > 2 || (sizes.size() == 2 && sizes.at(1) > 6)) {
        report.errors << rlTr("pacman", "The paths split into separate parts.");
    } else if (sizes.size() == 2) {
        report.warnings << rlTr("pacman", "The six-tile ghost-house inside is separate from the paths.");
    }
    // Runs of up to four equal tiles, never across a row.
    for (int row = 0; row < kRows; ++row) {
        int column = 0;
        while (column < kColumns) {
            int length = 1;
            while (column + length < kColumns && length < 4 && at(row, column + length) == at(row, column)) ++length;
            ++report.rleBytes;
            column += length;
        }
    }
    if (report.rleBytes > kRleBytes) {
        report.errors << rlTr("pacman", "The maze needs %1 RLE bytes; the ROM holds %2.").arg(report.rleBytes).arg(kRleBytes);
    }
    return report;
}

}  // namespace maze

PacmanPainter::PacmanPainter(const nes::Bundle& bundle) : bundle_(&bundle) {
    if (bundle.contains(ids::kChr)) chr_ = bundle.allPixels(ids::kChr);
    model_ = bundle.contains(ids::kGameplay) ? bundle.paletteModel(ids::kGameplay) : QStringLiteral("nes");
}

QColor PacmanPainter::color(int index) const {
    return QColor::fromRgb(nes::masterColor(index & 0x3f, model_.toStdString()));
}

QVector<int> PacmanPainter::colours(const QString& table, int palette) const {
    if (!bundle_->contains(table)) return {0x0f, 0x00, 0x10, 0x30};
    const QVector<int> values = bundle_->tableValues(table);
    QVector<int> result = values.mid(palette * 4, 4);
    while (result.size() < 4) result << 0x0f;
    return result;
}

QImage PacmanPainter::tile(int tile, const QVector<int>& colours, bool transparent) const {
    QImage image(8, 8, QImage::Format_ARGB32);
    for (int i = 0; i < 64; ++i) {
        const int at = tile * 64 + i;
        const int pen = at >= 0 && at < chr_.size() ? chr_.at(at) : 0;
        image.setPixel(i % 8, i / 8, transparent && pen == 0 ? qRgba(0, 0, 0, 0) : color(colours.value(pen)).rgba());
    }
    return image;
}

QImage PacmanPainter::maze(const QVector<int>& grid) const {
    // The game colours the whole maze with background palette 1.
    const QVector<int> palette = colours(ids::kGameplay, 1);
    QImage image(maze::kColumns * 8, maze::kRows * 8, QImage::Format_ARGB32);
    QPainter painter(&image);
    QHash<int, QImage> cache;
    for (int i = 0; i < grid.size() && i < maze::kRows * maze::kColumns; ++i) {
        const int t = grid.at(i);
        auto found = cache.find(t);
        if (found == cache.end()) found = cache.insert(t, tile(t, palette));
        painter.drawImage((i % maze::kColumns) * 8, (i / maze::kColumns) * 8, *found);
    }
    return image;
}

QVector<QVector<int>> PacmanPainter::spritePalettes(const QString& table) const {
    QVector<QVector<int>> result;
    const int first = bundle_->contains(table) && bundle_->tableValues(table).size() >= 32 ? 4 : 0;
    for (int p = 0; p < 4; ++p) result << colours(table, first + p);
    return result;
}

QImage PacmanPainter::frame(const QJsonObject& actors, bool alternate, int index, int palette,
                            const QVector<QVector<int>>& sprites) const {
    const QVector<QVector<int>> palettes = sprites.size() == 4 ? sprites : spritePalettes(ids::kGameplay);
    const QJsonArray tiles =
        actors.value(alternate ? QStringLiteral("alternate_tile_quads") : QStringLiteral("standard_tile_quads")).toArray();
    const QJsonArray attributes =
        actors.value(alternate ? QStringLiteral("alternate_attribute_quads") : QStringLiteral("standard_attribute_quads"))
            .toArray();
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    for (int q = 0; q < 4; ++q) {
        const int t = tiles.at(index).toArray().at(q).toInt();
        const int a = attributes.at(index).toArray().at(q).toInt();
        QImage sprite = tile(256 + t, palettes.at((palette | (a & 3)) & 3), true);
        sprite = sprite.mirrored((a & 0x40) != 0, (a & 0x80) != 0);
        painter.drawImage((q % 2) * 8, (q / 2) * 8, sprite);
    }
    return image;
}

QImage PacmanPainter::nameTable(const QVector<int>& cells, const QString& table) const {
    QImage image(256, 240, QImage::Format_ARGB32);
    QPainter painter(&image);
    QHash<int, QImage> cache;
    for (int cell = 0; cell < 960 && cell < cells.size(); ++cell) {
        const int x = cell % 32;
        const int y = cell / 32;
        const int attribute = cells.value(960 + (y / 4) * 8 + x / 4);
        const int palette = (attribute >> (((y % 4) / 2) * 4 + ((x % 4) / 2) * 2)) & 3;
        const int key = palette * 256 + (cells.at(cell) & 0xff);
        auto found = cache.find(key);
        if (found == cache.end()) found = cache.insert(key, tile(cells.at(cell) & 0xff, colours(table, palette)));
        painter.drawImage(x * 8, y * 8, *found);
    }
    return image;
}

}  // namespace rl::pacman
