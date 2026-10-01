#include "smb/render/level_image.hpp"

#include "nes/palette.hpp"

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

// Walker and Turtle groups repeat one enemy two or three times.
int groupMember(int id) {
    if (id == 0x37 || id == 0x38) return 0x06;
    if (id == 0x3b || id == 0x3c) return 0x00;
    return -1;
}

int groupCount(int id) {
    return id == 0x37 || id == 0x3b ? 2 : id == 0x38 || id == 0x3c ? 3 : 0;
}

// Six 8x8 tiles, two wide and three tall; 0xfc is the game's blank sprite.
QVector<int> enemyTiles(int id) {
    if (id == 0x14 || id == 0x15 || id == 0x17 || id == 0x34) {
        return {};
    }
    QVector<int> tiles;
    switch (id) {
    case 0x00: case 0x01: case 0x03: case 0x04: tiles = {0xfc, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9}; break;
    case 0x02: tiles = {0xfc, 0xfc, 0xaa, 0xab, 0xac, 0xad}; break;
    case 0x05: tiles = {0x7d, 0x7c, 0xd1, 0x8c, 0xd3, 0xd2}; break;
    case 0x06: tiles = {0xfc, 0xfc, 0x70, 0x71, 0x72, 0x73}; break;
    case 0x07: tiles = {0xfc, 0xfc, 0xdc, 0xdc, 0xdf, 0xdf}; break;
    case 0x08: tiles = {0xfc, 0xfc, 0xe8, 0xe7, 0xea, 0xe9}; break;
    case 0x09: case 0x0e: case 0x0f: case 0x10: tiles = {0x69, 0xa5, 0x6a, 0xa7, 0xa8, 0xa9}; break;
    case 0x0a: case 0x0b: tiles = {0xfc, 0xfc, 0xb2, 0xb3, 0xb4, 0xb5}; break;
    case 0x0c: tiles = {0xfc, 0xfc, 0xd0, 0xd0, 0xd7, 0xd7}; break;
    case 0x0d: tiles = {0xe5, 0xe5, 0xe6, 0xe6, 0xeb, 0xeb}; break;
    case 0x11: tiles = {0xb9, 0xb8, 0xbb, 0xba, 0xbc, 0xbc}; break;
    case 0x12: tiles = {0xfc, 0xfc, 0x96, 0x97, 0x98, 0x99}; break;
    case 0x16: tiles = {0xbf, 0xbe, 0xc1, 0xc0, 0xc2, 0xfc}; break;
    case 0x18: tiles = {0xf2, 0xf2, 0xf3, 0xf3, 0xf2, 0xf2}; break;
    case 0x19: tiles = {0xf1, 0xf1, 0xf1, 0xf1, 0xfc, 0xfc}; break;
    case 0x1a: tiles = {0xf0, 0xf0, 0xfc, 0xfc, 0xfc, 0xfc}; break;
    case 0x32: tiles = {0xf2, 0xf2, 0xf3, 0xf3, 0xf2, 0xf2}; break;
    case 0x35: tiles = {0xcd, 0xcd, 0xce, 0xce, 0xcf, 0xcf}; break;
    default: return {};
    }
    // Walking enemies face left: the game draws their columns swapped.
    if (id >= 0 && id <= 0x12) {
        for (int i = 0; i + 1 < tiles.size(); i += 2) {
            std::swap(tiles[i], tiles[i + 1]);
        }
    }
    return tiles;
}

int enemyPalette(int id) {
    if (isFirebar(id) || isLargeLift(id) || isSmallLift(id)) return 2;
    if (id == 0x37 || id == 0x38) return 3;
    if (id == 0x3b || id == 0x3c) return 1;
    if (id == 0x35) return 2;
    static constexpr std::array<int, 27> kRows{1, 2, 3, 2, 1, 1, 3, 3, 3, 1, 1, 2, 2, 1,
                                               1, 2, 1, 1, 2, 3, 2, 2, 1, 1, 2, 2, 2};
    return id >= 0 && id < int(kRows.size()) ? kRows[id] & 3 : 1;
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

// Small Hero standing: player frame 23 of the game's table.
constexpr int kStandingFrame = 23;

}  // namespace

QString areaTypeOf(const QString& areaName) {
    return areaName.section(QLatin1Char('_'), 0, -2);
}

bool automaticNight(const QString& areaType, const QVector<int>& header) {
    if (header.size() != 6) return false;
    static constexpr std::array<bool, 8> kNight{false, false, true, true, true, false, true, true};
    static const QStringList kTypes{QStringLiteral("water"), QStringLiteral("ground"), QStringLiteral("underground"),
                                    QStringLiteral("castle")};
    const int color = header.at(2) >= 4 ? header.at(2) : int(kTypes.indexOf(areaType));
    return color >= 0 && color < int(kNight.size()) && kNight[color];
}

QPoint entranceCell(const QVector<int>& header) {
    static constexpr std::array<int, 8> kStartY{0x00, 0x20, 0xb0, 0x50, 0x00, 0x00, 0xb0, 0xb0};
    const int entrance = header.size() == 6 ? std::clamp(header.at(1), 0, 7) : 0;
    return {2, std::max(1, (kStartY[entrance] - 0x10) / 0x10)};
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
    const QVector<int> palette = document_.packetValues(art_.palettes, areaType);
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
    const QVector<int> values = document_.packetValues(art_.palettes, packet);
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
    const QVector<int> palette = document_.packetValues(art_.palettes, areaType);
    const int start = nes::spriteEntry(enemyPalette(id), 0);
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
            drawTile(image, 0x64, QPoint(s * 6, s * 6));
        }
    } else if (isLargeLift(id) || isSmallLift(id)) {
        const int segments = isLargeLift(id) ? (areaType == QLatin1String("castle") ? 4 : 6) : 3;
        image = QImage(segments * 8, 8, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        for (int s = 0; s < segments; ++s) {
            drawTile(image, 0x5b, QPoint(s * 8, 0));
        }
    } else if (id == 0x2d) {  // Boss: rear and front halves
        const QVector<int> rear{0xc4, 0xc3, 0xc6, 0xc5, 0xc8, 0xc7};
        const QVector<int> front{0xbf, 0xbe, 0xc1, 0xc0, 0xc2, 0xfc};
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
        const QVector<int> tiles = enemyTiles(id);
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

QImage SmbLevelPainter::hero() const {
    const QString key = QStringLiteral("hero");
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
    const QVector<int> palette = art_.playerPalette.isEmpty() ? QVector<int>{} : document_.tableValues(art_.playerPalette);
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

QRect SmbLevelPainter::heroBounds(QPoint cell) const {
    const QImage image = hero();
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
