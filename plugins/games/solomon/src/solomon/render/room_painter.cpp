#include "solomon/render/room_painter.hpp"

#include "nes/palette.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QPainter>

namespace rl::solomon {

namespace {

constexpr int kEmpty = 0x10;  // RoomMap identity of an empty cell
constexpr int kBrownBlock = 0x90;
constexpr int kWhiteBlock = 0xf8;
constexpr int kKey = 0x06;

// A table the kit reads from the game into the graphics bundle's
// render-tables resource; empty in a project made before them.
QVector<int> renderTable(const nes::Bundle& graphics, const QString& name) {
    QVector<int> values;
    if (!graphics.contains(QStringLiteral("render-tables"))) return values;
    const QJsonArray raw = graphics.resource(QStringLiteral("render-tables"))
                               .value(QStringLiteral("tables"))
                               .toObject()
                               .value(name)
                               .toArray();
    for (const QJsonValue& value : raw) values.push_back(value.toInt());
    return values;
}

bool visible(QPoint p) { return p.x() >= 0 && p.x() < kWidth && p.y() >= 0 && p.y() < kHeight; }

int identityPattern(int value) {
    if (value >= 0xf8) return 3;
    if (value >= 0x80) return 0;
    if (value >= 0x40) return kEmpty;
    return value < 58 ? value : kEmpty;
}

QRgb rgb(int index) { return 0xff000000u | nes::masterColor(index & 0x3f); }

struct Placement {
    int type = 0;
    QPoint position;
};

}  // namespace

const SolomonPainter::Bank& SolomonPainter::bank(int index) const {
    auto found = banks_.find(index);
    if (found == banks_.end()) {
        Bank b;
        const QString id = QStringLiteral("chr-bank-%1").arg(index);
        if (state_->graphics.contains(id)) {
            b.pixels = state_->graphics.allPixels(id);
        }
        b.pixels.resize(512 * 64);
        found = banks_.insert(index, b);
    }
    return *found;
}

QVector<int> SolomonPainter::palette(int group) const {
    QVector<int> values = state_->graphics.tableValues(QStringLiteral("room-palettes"));
    values.resize(std::max(32, int(values.size())));
    int color = groupColor(state_->art, group);
    if (color == 0x80) {
        values[10] = 0x16;
        color = 0;
    }
    for (int index : {1, 5, 9, 13}) values[index] = color;
    return values;
}

void SolomonPainter::drawPattern(QImage& image, int x, int y, const SolomonPattern& p, const QVector<int>& palette,
                                 const Bank& b, int opacity, bool transparentZero) const {
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const int tile = 0x100 + (p.tiles[quadrant] & 0xff);
        for (int ty = 0; ty < 8; ++ty) {
            for (int tx = 0; tx < 8; ++tx) {
                const int pen = b.pixels.at(tile * 64 + ty * 8 + tx);
                if (transparentZero && pen == 0) continue;
                const int ox = x * kCell + (quadrant & 1) * 8 + tx;
                const int oy = y * kCell + (quadrant >> 1) * 8 + ty;
                if (ox < 0 || oy < 0 || ox >= image.width() || oy >= image.height()) continue;
                const QRgb color = rgb(palette.value(p.palette * 4 + pen));
                if (opacity >= 255) {
                    image.setPixel(ox, oy, color);
                } else {
                    const QRgb before = image.pixel(ox, oy);
                    const auto mix = [opacity](int a, int b) {
                        return (a * (255 - opacity) + b * opacity + 127) / 255;
                    };
                    image.setPixel(ox, oy,
                                   qRgb(mix(qRed(before), qRed(color)), mix(qGreen(before), qGreen(color)),
                                        mix(qBlue(before), qBlue(color))));
                }
            }
        }
    }
}

QImage SolomonPainter::framedRoom(int index, const Options& options, const SolomonRooms* draft) const {
    const QImage inner = room(index, options, draft);
    QImage image(kWidth * kCell, (kHeight + 1) * kCell, QImage::Format_RGB32);
    image.fill(Qt::black);
    {
        QPainter painter(&image);
        painter.drawImage(QPoint(8, 0), inner, QRect(0, 0, (kWidth - 1) * kCell, kHeight * kCell));
    }
    // DrawRoomNametableFrame: the room fills name table columns 0-29 from
    // row 4; columns 30 and 31 and rows 28-29 hold the frame, and the screen
    // is scrolled 8 pixels, so column 31 shows at the left. Each four-byte
    // pattern repeats down a column or across a row, its last byte first.
    const QVector<int> patterns = renderTable(state_->graphics, QStringLiteral("room_frame_patterns"));
    if (patterns.size() < 16) return image;
    const SolomonRooms& rooms = draft ? *draft : state_->rooms;
    const Bank& b = bank(SolomonRooms::chrBank(rooms.itemCommands(index)));
    const QVector<int> colors = palette(groupOf(index));
    const auto drawTile = [&](int tile, int left, int top) {
        for (int ty = 0; ty < 8; ++ty) {
            for (int tx = 0; tx < 8; ++tx) {
                const int pen = b.pixels.at((0x100 + (tile & 0xff)) * 64 + ty * 8 + tx);
                image.setPixel(left + tx, top + ty, rgb(colors.value(pen)));
            }
        }
    };
    const auto screenX = [](int column) { return column == 31 ? 0 : column == 30 ? 248 : 8 + column * 8; };
    for (int row = 4; row < 28; ++row) {
        const int phase = 3 - (row - 4) % 4;
        drawTile(patterns.at(phase), screenX(31), (row - 4) * 8);
        drawTile(patterns.at(4 + phase), screenX(30), (row - 4) * 8);
    }
    for (int column = 0; column < 32; ++column) {
        const int phase = 3 - column % 4;
        drawTile(patterns.at(8 + phase), screenX(column), 24 * 8);
        drawTile(patterns.at(12 + phase), screenX(column), 25 * 8);
    }
    return image;
}

void SolomonPainter::drawSprite(QImage& image, const QJsonObject& frame, const Bank& b, int x, int y,
                                bool mirrorY) const {
    if (frame.isEmpty()) return;
    const QVector<int> values = state_->graphics.tableValues(QStringLiteral("room-palettes"));
    const int flags = frame.value(QStringLiteral("flags")).toInt();
    const int attributes[2] = {
        ((flags & 0x20) << 2) | ((flags & 0x10) << 2) | ((flags & 0x80) >> 7) | ((flags & 0x40) >> 5),
        ((flags & 0x01) << 7) | ((flags & 0x02) << 5) | ((flags & 0x08) >> 2) | ((flags & 0x04) >> 2),
    };
    const int tiles[2] = {frame.value(QStringLiteral("left_tile")).toInt(),
                          frame.value(QStringLiteral("right_tile")).toInt()};
    for (int half = 0; half < 2; ++half) {
        const int attribute = attributes[half];
        const bool hflip = attribute & 0x40;
        const bool vflip = attribute & 0x80;
        const int paletteOffset = (attribute & 3) * 4;
        const int base = (tiles[half] & 1) * 0x100;
        const int top = tiles[half] & 0xfe;
        for (int sy = 0; sy < 16; ++sy) {
            const int tile = base + top + sy / 8;
            const int outY = vflip ? 15 - sy : sy;
            const int py = y * kCell + (mirrorY ? 15 - outY : outY);
            if (py < 0 || py >= image.height()) continue;
            for (int sx = 0; sx < 8; ++sx) {
                const int pen = b.pixels.at(tile * 64 + (sy & 7) * 8 + sx);
                if (pen == 0) continue;
                const int px = x * kCell + half * 8 + (hflip ? 7 - sx : sx);
                if (px < 0 || px >= image.width()) continue;
                image.setPixel(px, py, rgb(values.value(16 + paletteOffset + pen)));
            }
        }
    }
}

QImage SolomonPainter::patternIcon(int index, int bankIndex, int group, bool transparentZero) const {
    QImage image(kCell, kCell, QImage::Format_ARGB32);
    image.fill(transparentZero ? Qt::transparent : QColor(Qt::black));
    drawPattern(image, 0, 0, pattern(state_->art, index), palette(group), bank(bankIndex), 255, transparentZero);
    return image;
}

QImage SolomonPainter::enemyIcon(int type, int bankIndex) const {
    const QJsonObject frame = enemyFrame(state_->animations, type);
    if (frame.isEmpty()) return {};
    QImage image(kCell, kCell, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    drawSprite(image, frame, bank(bankIndex), 0, 0, type >= 0x50 && type < 0x80);
    return image;
}

QImage SolomonPainter::frameImage(const QJsonObject& frame, int bankIndex) const {
    QImage image(kCell, kCell, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    drawSprite(image, frame, bank(bankIndex), 0, 0);
    return image;
}

QImage SolomonPainter::spritePair(int tile, int bankIndex, int palette) const {
    QImage image(8, 16, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const Bank& b = bank(bankIndex);
    const QVector<int> values = state_->graphics.tableValues(QStringLiteral("room-palettes"));
    const int base = (tile & 1) * 0x100 + (tile & 0xfe);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int pen = b.pixels.at((base + y / 8) * 64 + (y & 7) * 8 + x);
            if (pen) image.setPixel(x, y, rgb(values.value(16 + (palette & 3) * 4 + pen)));
        }
    }
    return image;
}

QImage SolomonPainter::playerIcon(int bankIndex, bool facesLeft) const {
    QImage image(kCell, kCell, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    drawSprite(image, playerFrame(state_->animations, facesLeft), bank(bankIndex), 0, 0);
    return image;
}

QImage SolomonPainter::room(int index, const Options& options, const SolomonRooms* draft) const {
    const SolomonRooms& rooms = draft ? *draft : state_->rooms;
    const QJsonArray commands = rooms.itemCommands(index);
    const int bankIndex = SolomonRooms::chrBank(commands);
    const Bank& b = bank(bankIndex);
    const QVector<int> colors = palette(groupOf(index));
    QImage image(kWidth * kCell, kHeight * kCell, QImage::Format_RGB32);
    image.fill(Qt::black);

    int cells[kHeight][kWidth];
    int embedded[kHeight][kWidth];
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            cells[y][x] = kEmpty;
            embedded[y][x] = -1;
            if (rooms.block(index, QPoint(x, y), false)) cells[y][x] = kBrownBlock;
            if (rooms.block(index, QPoint(x, y), true)) cells[y][x] = kWhiteBlock;
        }
    }
    const auto setCell = [&cells](QPoint p, int value) {
        if (visible(p)) cells[p.y()][p.x()] = value;
    };
    const QPoint door = rooms.metadataPosition(index, QStringLiteral("door"));
    const QPoint key = rooms.metadataPosition(index, QStringLiteral("key"));
    const int status = rooms.metadata(index).value(QStringLiteral("status_rate_raw")).toInt();
    const int keyBits = status >= 0x80 ? 0x40 : status >= 0x40 ? 0x80 : 0;
    setCell(door, 0x02);
    if (visible(key)) {
        setCell(key, keyBits | kKey);
        if (keyBits == 0x80) embedded[key.y()][key.x()] = 0x86;
    } else if (visible(door)) {
        setCell(door, 0x35);
    }
    setCell(rooms.metadataPosition(index, QStringLiteral("mirror_1")), 0x05);
    setCell(rooms.metadataPosition(index, QStringLiteral("mirror_2")), 0x05);

    QVector<Placement> placements;
    const QJsonObject special = rooms.specialRooms();
    if (options.items) {
        if (index == 50) {
            // The random bonus room: one of 32 layouts of 16 items.
            const QJsonObject bonus = special.value(QStringLiteral("random_bonus_room")).toObject();
            const QJsonArray positions = bonus.value(QStringLiteral("positions")).toArray();
            const QJsonArray types = bonus.value(QStringLiteral("item_types")).toArray();
            if (positions.size() == 32 && types.size() == 16) {
                int at = options.bonusLayout & 31;
                for (int item = 15; item >= 0; --item) {
                    placements << Placement{types.at(item).toInt(), point(positions.at(at))};
                    at = (at - 1) & 31;
                }
            }
        }
        for (const QJsonValue& value : commands) {
            const QJsonObject command = value.toObject();
            const QString kind = command.value(QStringLiteral("kind")).toString();
            const int type = command.value(QStringLiteral("type")).toInt(-1);
            if (kind == QLatin1String("item")) {
                placements << Placement{type, point(command.value(QStringLiteral("position")))};
            } else if (kind == QLatin1String("repeat")) {
                for (const QJsonValue& p : command.value(QStringLiteral("positions")).toArray()) {
                    placements << Placement{type, point(p)};
                }
            }
        }
        for (const Placement& placement : placements) {
            if (!visible(placement.position)) continue;
            setCell(placement.position, placement.type);
            if (placement.type & 0x80) embedded[placement.position.y()][placement.position.x()] = placement.type;
        }
    }
    QJsonObject constellation;
    for (const QJsonValue& value : commands) {
        if (value.toObject().value(QStringLiteral("kind")).toString() == QLatin1String("constellation")) {
            constellation = value.toObject();
        }
    }
    // The six patterns of each constellation and its palette, as the game's
    // tables list them.
    const QVector<int> constellationTiles =
        renderTable(state_->graphics, QStringLiteral("constellation_tile_patterns"));
    const QVector<int> constellationPalettes = renderTable(state_->graphics, QStringLiteral("constellation_palettes"));
    const auto constellationAt = [&](int x, int y, SolomonPattern* result) {
        if (constellation.isEmpty() || constellationTiles.size() < 96 || constellationPalettes.size() < 12)
            return false;
        const QPoint origin = point(constellation.value(QStringLiteral("position")));
        const int rx = x - origin.x();
        const int ry = y - origin.y();
        if (rx < 0 || rx >= 3 || ry < 0 || ry >= 2) return false;
        const int opcode = constellation.value(QStringLiteral("opcode")).toInt(-1);
        if (opcode < 0xf0 || opcode > 0xfb) return false;
        const int offset = (opcode & 3) * 24 + (ry * 3 + rx) * 4;
        result->palette = constellationPalettes.at(opcode & 15);
        result->tiles[0] = constellationTiles.at(offset) & 0xfc;
        result->tiles[1] = constellationTiles.at(offset + 1);
        result->tiles[2] = constellationTiles.at(offset + 2);
        result->tiles[3] = constellationTiles.at(offset + 3);
        return true;
    };

    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const int value = cells[y][x];
            const bool combined = value == kWhiteBlock && rooms.block(index, QPoint(x, y), false) &&
                                  rooms.block(index, QPoint(x, y), true);
            const bool embeddedHere = embedded[y][x] == value;
            SolomonPattern chosen;
            if ((value == kEmpty || combined || embeddedHere) && constellationAt(x, y, &chosen)) {
                drawPattern(image, x, y, chosen, colors, b);
            } else {
                drawPattern(image, x, y,
                            pattern(state_->art, combined || embeddedHere ? kEmpty : identityPattern(value)), colors,
                            b);
            }
            if (combined || embeddedHere) {
                drawPattern(image, x, y, pattern(state_->art, embeddedHere ? 0 : 3), colors, b, embeddedHere ? 80 : 128,
                            true);
            }
        }
    }
    // Seals, and the bat symbols and blue opals of rooms 20 and 30.
    for (const QJsonValue& value : special.value(QStringLiteral("solomon_seals")).toArray()) {
        const QJsonObject record = value.toObject();
        const QPoint p = point(record.value(QStringLiteral("position")));
        if (record.value(QStringLiteral("room")).toInt() == index + 1 && visible(p)) {
            drawPattern(image, p.x(), p.y(), pattern(state_->art, 0x20), colors, b);
        }
    }
    const QString field = index == 19   ? QStringLiteral("room_20_bat_symbols")
                          : index == 29 ? QStringLiteral("room_30_blue_opals")
                                        : QString();
    if (!field.isEmpty()) {
        const int symbol = index == 19 ? 0x04 : 0x27;
        for (const QJsonValue& value : special.value(field).toArray()) {
            const QPoint p = point(value);
            if (visible(p)) drawPattern(image, p.x(), p.y(), pattern(state_->art, symbol), colors, b);
        }
    }
    // Items hidden in blocks show faintly through them.
    for (const Placement& placement : placements) {
        const int type = placement.type;
        if (!(type & 0xc0) || !visible(placement.position)) continue;
        if (cells[placement.position.y()][placement.position.x()] != type) continue;
        const int identity = type & 0x3f;
        drawPattern(image, placement.position.x(), placement.position.y(),
                    pattern(state_->art, identity < 58 ? identity : kEmpty), colors, b, type & 0x80 ? 192 : 128, true);
    }
    if (keyBits != 0 && visible(key) && cells[key.y()][key.x()] == (keyBits | kKey)) {
        drawPattern(image, key.x(), key.y(), pattern(state_->art, kKey), colors, b, keyBits == 0x80 ? 192 : 128, true);
    }
    if (options.start) {
        const QPoint start = rooms.metadataPosition(index, QStringLiteral("player_start"));
        if (visible(start)) {
            drawSprite(image, playerFrame(state_->animations, start.x() >= kWidth / 2), b, start.x(), start.y());
        }
    }
    if (options.enemies) {
        for (const SolomonEnemy& enemy : rooms.enemies(index)) {
            if (!visible(enemy.position)) continue;
            drawSprite(image, enemyFrame(state_->animations, enemy.type), b, enemy.position.x(), enemy.position.y(),
                       enemy.type >= 0x50 && enemy.type < 0x80);
        }
    }
    return image;
}

}  // namespace rl::solomon
