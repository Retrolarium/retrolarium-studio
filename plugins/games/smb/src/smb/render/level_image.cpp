#include "smb/render/level_image.hpp"

#include "nes/palette.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>
#include <array>

namespace rl::smb {

namespace {

// Enemy art, as the game composes it: ported from the original app's level
// editor. Identifiers are the enemy object numbers of the enemy stream.
bool isFirebar(int id) { return id >= 0x1b && id <= 0x1f; }
bool isLargeLift(int id) { return id >= 0x24 && id <= 0x2a; }
bool isSmallLift(int id) { return id == 0x2b || id == 0x2c; }

// Enemy groups ($37-$3E) repeat one enemy two or three times.
int groupMember(int id) {
    if (id == 0x37 || id == 0x38) return 0x06;
    if (id == 0x3b || id == 0x3c) return 0x00;
    return -1;
}

int groupCount(int id) { return id == 0x37 || id == 0x3b ? 2 : id == 0x38 || id == 0x3c ? 3 : 0; }

// Six 8x8 tiles, two wide and three tall, from the game's enemy graphics
// table at the enemy's offset; 0xfc is the game's blank sprite. Enemies the
// level view does not draw (frenzies, the boss's fire) and offset 0xff have
// none; the jumpspring object draws like the jumpspring enemy, and the
// retainer is the row after the princess.
QVector<int> enemyTiles(int id, const QVector<int>& graphics, const QVector<int>& offsets) {
    if (id == 0x14 || id == 0x15 || id == 0x17 || id == 0x34) {
        return {};
    }
    int offset = -1;
    if (id == 0x32 && offsets.size() > 0x18) offset = offsets.at(0x18);
    else if (id == 0x35 && offsets.size() > 0x15) offset = offsets.at(0x15) + 6;
    else if (id >= 0 && id < offsets.size() && offsets.at(id) != 0xff) offset = offsets.at(id);
    if (offset < 0 || offset + 6 > graphics.size()) {
        return {};
    }
    QVector<int> tiles = graphics.mid(offset, 6);
    // Walking enemies face left: the game draws their columns swapped.
    if (id >= 0 && id <= 0x12) {
        for (int i = 0; i + 1 < tiles.size(); i += 2) {
            std::swap(tiles[i], tiles[i + 1]);
        }
    }
    return tiles;
}

// The sprite palette: firebars and lifts load theirs in code, groups and the
// retainer take their member's, the rest come from the attribute table.
int enemyPalette(int id, const QVector<int>& attributes, int firebar, int platform) {
    if (isFirebar(id)) return firebar & 3;
    if (isLargeLift(id) || isSmallLift(id)) return platform & 3;
    if (groupMember(id) >= 0) return enemyPalette(groupMember(id), attributes, firebar, platform);
    if (id == 0x35) return enemyPalette(0x15, attributes, firebar, platform);
    return id >= 0 && id < attributes.size() ? attributes.at(id) & 3 : 1;
}

QVector<bool> horizontalFlips(int id) {
    if (id == 0x07 || id == 0x0c || id == 0x0d || id == 0x35 || id == 0x32) {
        return {false, true, false, true, false, true};
    }
    if (id == 0x11) return {true, true, true, true, false, true};
    if (id >= 0 && id <= 0x12) return QVector<bool>(6, true);
    return {};
}

QVector<bool> verticalFlips(int id) {
    return id == 0x32 ? QVector<bool>{false, false, true, true, true, true} : QVector<bool>{};
}

// Top of the sprite in level pixels; the engine draws most enemies 8px low.
int enemyTop(const SmbEnemyRecord& record) {
    if (record.objectOrPage == 0x35) return 0xb8 - 32;
    const int engineOffset = record.objectOrPage < 0x15 ? 8 : 0;
    return std::max(0, (record.row - 2) * 16 + engineOffset);
}

// The small player standing: player frame 23 of the game's table.
constexpr int kStandingFrame = 23;

}  // namespace

QString areaTypeOf(const QString& areaName) { return areaName.section(QLatin1Char('_'), 0, -2); }

QVector<int> smbLevelTable(const SmbResourceDocument& document, const QString& name) {
    QVector<int> values;
    const QJsonArray raw =
        document.resource(kLevelTables).value(QStringLiteral("tables")).toObject().value(name).toArray();
    for (const QJsonValue& value : raw) values.push_back(value.toInt());
    return values;
}

bool automaticNight(const QString& areaType, const QVector<int>& header, const QVector<int>& colors) {
    if (header.size() != 6 || colors.isEmpty()) return false;
    static const QStringList kTypes{QStringLiteral("water"), QStringLiteral("ground"), QStringLiteral("underground"),
                                    QStringLiteral("castle")};
    // A backdrop other than the water areas' daytime colour is a night sky.
    const int color = header.at(2) >= 4 ? header.at(2) : int(kTypes.indexOf(areaType));
    return color >= 0 && color < colors.size() && colors.at(color) != colors.at(0);
}

QPoint entranceCell(const QVector<int>& header, const QVector<int>& startY) {
    const int entrance = header.size() == 6 ? std::clamp(header.at(1), 0, 7) : 0;
    if (entrance >= startY.size()) return {2, 1};
    return {2, std::max(1, (startY.at(entrance) - 0x10) / 0x10)};
}

SmbLevelPainter::SmbLevelPainter(const SmbResourceDocument& document, SmbArt art)
    : document_(document), art_(std::move(art)) {}

void SmbLevelPainter::clearCache() {
    cache_.clear();
    model_.clear();
}

QRgb SmbLevelPainter::color(int index) const {
    if (model_.empty()) {
        model_ = document_.paletteModel(art_.palettes).toStdString();
    }
    return 0xff000000u | nes::masterColor(index, model_);
}

QImage SmbLevelPainter::metatile(int value, const QString& areaType, bool night) const {
    const QString key = QStringLiteral("m%1|%2|%3").arg(value).arg(areaType).arg(night);
    const auto found = cache_.constFind(key);
    if (found != cache_.constEnd()) {
        return *found;
    }
    QImage image(16, 16, QImage::Format_ARGB32);
    const QVector<int> palette = document_.paletteFor(art_.palettes, areaType);
    const std::array<int, 4> tiles = document_.metatileTiles(art_.metatiles, value);
    const int start = SmbResourceDocument::metatilePalette(value) * 4;
    const QRgb backdrop =
        color(night ? document_.nightBackground(art_.palettes) : document_.dayBackground(art_.palettes));
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        for (int y = 0; y < 8; ++y) {
            auto* line = reinterpret_cast<QRgb*>(image.scanLine((quadrant / 2) * 8 + y)) + (quadrant % 2) * 8;
            for (int x = 0; x < 8; ++x) {
                const int pixel = document_.pixel(art_.chr, tiles[quadrant], x, y);
                line[x] = pixel == 0 || start + pixel >= palette.size() ? backdrop : color(palette.at(start + pixel));
            }
        }
    }
    cache_.insert(key, image);
    return image;
}

QImage SmbLevelPainter::backgroundTile(int tile, const QString& packet, int palette, int backdrop) const {
    const QString key = QStringLiteral("b%1|%2|%3|%4").arg(tile).arg(packet).arg(palette).arg(backdrop);
    const auto found = cache_.constFind(key);
    if (found != cache_.constEnd()) {
        return *found;
    }
    QImage image(8, 8, QImage::Format_ARGB32);
    const QVector<int> values = document_.paletteFor(art_.palettes, packet);
    const int start = nes::backgroundEntry(palette & 3, 0);
    const int chrTile = 256 + (tile & 0xff);
    const bool inTable = chrTile < document_.tileCount(art_.chr);
    for (int y = 0; y < 8; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < 8; ++x) {
            const int pixel = inTable ? document_.pixel(art_.chr, chrTile, x, y) : 0;
            line[x] = pixel == 0 || start + pixel >= values.size() ? color(backdrop) : color(values.at(start + pixel));
        }
    }
    cache_.insert(key, image);
    return image;
}

QImage SmbLevelPainter::enemy(int id, const QString& areaType) const {
    const QString key = QStringLiteral("e%1|%2").arg(id).arg(areaType);
    const auto found = cache_.constFind(key);
    if (found != cache_.constEnd()) {
        return *found;
    }
    const QVector<int> graphics = smbLevelTable(document_, QStringLiteral("enemy_graphics_tiles"));
    const QVector<int> offsets = smbLevelTable(document_, QStringLiteral("enemy_graphics_offsets"));
    const QVector<int> attributes = smbLevelTable(document_, QStringLiteral("enemy_sprite_attributes"));
    const QVector<int> firebar = smbLevelTable(document_, QStringLiteral("firebar_tile"));
    const QVector<int> platform = smbLevelTable(document_, QStringLiteral("platform_tile"));
    const QVector<int> firebarAttributes = smbLevelTable(document_, QStringLiteral("firebar_attributes"));
    const QVector<int> platformAttributes = smbLevelTable(document_, QStringLiteral("platform_attributes"));
    if (graphics.isEmpty() || offsets.size() <= 0x18 || attributes.isEmpty() || firebar.isEmpty() ||
        platform.isEmpty() || firebarAttributes.isEmpty() || platformAttributes.isEmpty()) {
        return {};  // a project made before the level tables
    }
    const QVector<int> palette = document_.paletteFor(art_.palettes, areaType);
    const int start =
        nes::spriteEntry(enemyPalette(id, attributes, firebarAttributes.at(0), platformAttributes.at(0)), 0);
    if (start + 3 >= palette.size()) {
        return {};
    }
    const int tileCount = document_.tileCount(art_.chr);
    const auto drawTile = [&](QImage& image, int tile, QPoint at, bool flipX = false, bool flipY = false) {
        if (tile == 0xfc || tile < 0 || tile >= tileCount) {
            return;
        }
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const int pixel = document_.pixel(art_.chr, tile, x, y);
                const QPoint target(at.x() + (flipX ? 7 - x : x), at.y() + (flipY ? 7 - y : y));
                if (pixel != 0 && image.rect().contains(target)) {
                    image.setPixel(target, color(palette.at(start + pixel)));
                }
            }
        }
    };
    QImage image;
    if (isFirebar(id)) {
        const int segments = id == 0x1f ? 11 : 5;
        image = QImage((segments - 1) * 6 + 8, (segments - 1) * 6 + 8, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        for (int s = 0; s < segments; ++s) {
            drawTile(image, firebar.at(0), QPoint(s * 6, s * 6));
        }
    } else if (isLargeLift(id) || isSmallLift(id)) {
        const int segments =
            isLargeLift(id) ? (areaType.section(QLatin1Char('+'), 0, 0) == QLatin1String("castle") ? 4 : 6) : 3;
        image = QImage(segments * 8, 8, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        for (int s = 0; s < segments; ++s) {
            drawTile(image, platform.at(0), QPoint(s * 8, 0));
        }
    } else if (id == 0x2d) {  // The boss: rear and front halves, its enemy rows
        const QVector<int> rear = graphics.mid(offsets.at(0x17), 6);
        const QVector<int> front = graphics.mid(offsets.at(0x16), 6);
        image = QImage(32, 24, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        for (int i = 0; i < rear.size(); ++i) {
            drawTile(image, rear.at(i), QPoint((i % 2) * 8, (i / 2) * 8));
            drawTile(image, front.at(i), QPoint(16 + (i % 2) * 8, (i / 2) * 8));
        }
    } else if (groupMember(id) >= 0) {
        const QImage member = enemy(groupMember(id), areaType);
        if (member.isNull()) {
            return {};
        }
        image = QImage(member.width() * groupCount(id), member.height(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        for (int i = 0; i < groupCount(id); ++i) {
            painter.drawImage(i * member.width(), 0, member);
        }
    } else {
        const QVector<int> tiles = enemyTiles(id, graphics, offsets);
        if (tiles.isEmpty()) {
            return {};
        }
        const QVector<bool> flipX = horizontalFlips(id);
        const QVector<bool> flipY = verticalFlips(id);
        image = QImage(16, 24, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        for (int i = 0; i < tiles.size(); ++i) {
            drawTile(image, tiles.at(i), QPoint((i % 2) * 8, (i / 2) * 8), i < flipX.size() && flipX.at(i),
                     i < flipY.size() && flipY.at(i));
        }
    }
    cache_.insert(key, image);
    return image;
}

QRect SmbLevelPainter::enemyBounds(const SmbPositionedEnemy& enemy, const QString& areaType) const {
    const QImage sprite = this->enemy(enemy.record.objectOrPage, areaType);
    const QSize size = sprite.isNull() ? QSize(16, 16) : sprite.size();
    // The first fireball sits centred on the holder's 16x16 cell.
    const int inset = isFirebar(enemy.record.objectOrPage) ? 4 : 0;
    return {enemy.x * kCell + inset, enemyTop(enemy.record) + inset, size.width(), size.height()};
}

QImage SmbLevelPainter::player() const {
    const QString key = QStringLiteral("player");
    const auto found = cache_.constFind(key);
    if (found != cache_.constEnd()) {
        return *found;
    }
    if (art_.playerFrames.isEmpty() || document_.playerFrameCount(art_.playerFrames) <= kStandingFrame) {
        return {};
    }
    const QVector<int> tiles = document_.playerFrameTiles(art_.playerFrames, kStandingFrame);
    const int base = document_.playerTileBase(art_.playerFrames);
    const int blank = document_.playerBlankTile(art_.playerFrames);
    const QVector<int> palette =
        art_.playerPalette.isEmpty() ? QVector<int>{} : document_.tableValues(art_.playerPalette);
    if (palette.size() < 4) {
        return {};
    }
    QRect bounds;
    for (int i = 0; i < tiles.size(); ++i) {
        if (tiles.at(i) != blank) {
            bounds |= QRect((i & 1) * 8, (i / 2) * 8, 8, 8);
        }
    }
    if (bounds.isEmpty()) {
        return {};
    }
    QImage image(bounds.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    for (int i = 0; i < tiles.size(); ++i) {
        if (tiles.at(i) == blank) {
            continue;
        }
        // SMB draws symmetrical body rows from the left half and flips the
        // right sprite; the standing frame does so on its last row.
        const bool mirrored = i == 7 && kStandingFrame == 23;
        const QPoint at = QPoint((i & 1) * 8, (i / 2) * 8) - bounds.topLeft();
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const int pixel = document_.pixel(art_.chr, base + tiles.at(i), mirrored ? 7 - x : x, y);
                if (pixel != 0) {
                    image.setPixel(at.x() + x, at.y() + y, color(palette.at(pixel)));
                }
            }
        }
    }
    cache_.insert(key, image);
    return image;
}

QRect SmbLevelPainter::playerBounds(QPoint cell) const {
    const QImage image = player();
    const QSize size = image.isNull() ? QSize(16, 32) : image.size();
    return {cell.x() * kCell, (cell.y() + 1) * kCell - size.height(), size.width(), size.height()};
}

QImage SmbLevelPainter::render(const SmbLevelScene& scene, const QString& areaType, bool night) const {
    QImage image(std::max(1, scene.width) * kCell, kRows * kCell, QImage::Format_ARGB32);
    image.fill(color(night ? document_.nightBackground(art_.palettes) : document_.dayBackground(art_.palettes)));
    QPainter painter(&image);
    for (int column = 0; column < scene.metatiles.size(); ++column) {
        const QVector<int>& cells = scene.metatiles.at(column);
        for (int row = 0; row < cells.size() && row < kRows; ++row) {
            painter.drawImage(column * kCell, row * kCell, metatile(cells.at(row), areaType, night));
        }
    }
    for (const SmbPositionedEnemy& e : scene.enemies) {
        const QImage sprite = enemy(e.record.objectOrPage, areaType);
        if (!sprite.isNull()) {
            painter.drawImage(enemyBounds(e, areaType).topLeft(), sprite);
        }
    }
    return image;
}

}  // namespace rl::smb
