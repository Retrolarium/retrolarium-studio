#include "solomon/render/room_painter.hpp"

#include "nes/palette.hpp"

#include <array>

namespace rl::solomon {

namespace {

constexpr int kEmpty = 0x10;        // RoomMap identity of an empty cell
constexpr int kBrownBlock = 0x90;
constexpr int kWhiteBlock = 0xf8;
constexpr int kKey = 0x06;

// The six patterns of each constellation, as the game's table lists them.
constexpr std::array<unsigned char, 96> kConstellationTiles{
    0x28, 0xc1, 0x2a, 0xc3, 0xc0, 0xc5, 0xc2, 0xc7, 0xc4, 0x29, 0xc6, 0x2b, 0x28, 0xc9, 0x2a, 0xcb,
    0xc8, 0xcd, 0xca, 0xcf, 0xcc, 0x29, 0xce, 0x2b, 0x28, 0xd1, 0x2a, 0xd3, 0xd0, 0xd5, 0xd2, 0xd7,
    0xd4, 0x29, 0xd6, 0x2b, 0x28, 0xd9, 0x2a, 0xdb, 0xd8, 0xdd, 0xda, 0xdf, 0xdc, 0x29, 0xde, 0x2b,
    0x28, 0xe1, 0x2a, 0xe3, 0xe0, 0xe5, 0xe2, 0xe7, 0xe4, 0x29, 0xe6, 0x2b, 0x28, 0xe9, 0x2a, 0xeb,
    0xe8, 0xed, 0xea, 0xef, 0xec, 0x29, 0xee, 0x2b, 0x28, 0xf1, 0x2a, 0xf3, 0xf0, 0xf5, 0xf2, 0xf7,
    0xf4, 0x29, 0xf6, 0x2b, 0x28, 0xf9, 0x2a, 0xfb, 0xf8, 0xfd, 0xfa, 0xff, 0xfc, 0x29, 0xfe, 0x2b,
};
constexpr std::array<int, 12> kConstellationPalettes{3, 3, 3, 3, 2, 3, 0, 2, 0, 1, 0, 1};

bool visible(QPoint p) {
    return p.x() >= 0 && p.x() < kWidth && p.y() >= 0 && p.y() < kHeight;
}

int identityPattern(int value) {
    if (value >= 0xf8) return 3;
    if (value >= 0x80) return 0;
    if (value >= 0x40) return kEmpty;
    return value < 58 ? value : kEmpty;
}

QRgb rgb(int index) {
    return 0xff000000u | nes::masterColor(index & 0x3f);
}

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
                    const auto mix = [opacity](int a, int b) { return (a * (255 - opacity) + b * opacity + 127) / 255; };
                    image.setPixel(ox, oy, qRgb(mix(qRed(before), qRed(color)), mix(qGreen(before), qGreen(color)),
                                                mix(qBlue(before), qBlue(color))));
                }
            }
        }
    }
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
    const int tiles[2] = {frame.value(QStringLiteral("left_tile")).toInt(), frame.value(QStringLiteral("right_tile")).toInt()};
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

QImage SolomonPainter::wizardIcon(int bankIndex, bool facesLeft) const {
    QImage image(kCell, kCell, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    drawSprite(image, wizardFrame(state_->animations, facesLeft), bank(bankIndex), 0, 0);
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
    const auto constellationAt = [&constellation](int x, int y, SolomonPattern* result) {
        if (constellation.isEmpty()) return false;
        const QPoint origin = point(constellation.value(QStringLiteral("position")));
        const int rx = x - origin.x();
        const int ry = y - origin.y();
        if (rx < 0 || rx >= 3 || ry < 0 || ry >= 2) return false;
        const int opcode = constellation.value(QStringLiteral("opcode")).toInt(-1);
        if (opcode < 0xf0 || opcode > 0xfb) return false;
        const int offset = (opcode & 3) * 24 + (ry * 3 + rx) * 4;
        result->palette = kConstellationPalettes.at(opcode & 15);
        result->tiles[0] = kConstellationTiles.at(offset) & 0xfc;
        result->tiles[1] = kConstellationTiles.at(offset + 1);
        result->tiles[2] = kConstellationTiles.at(offset + 2);
        result->tiles[3] = kConstellationTiles.at(offset + 3);
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
                drawPattern(image, x, y, pattern(state_->art, combined || embeddedHere ? kEmpty : identityPattern(value)),
                            colors, b);
            }
            if (combined || embeddedHere) {
                drawPattern(image, x, y, pattern(state_->art, embeddedHere ? 0 : 3), colors, b, embeddedHere ? 80 : 128, true);
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
    const QString field = index == 19 ? QStringLiteral("room_20_bat_symbols")
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
            drawSprite(image, wizardFrame(state_->animations, start.x() >= kWidth / 2), b, start.x(), start.y());
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
