#include "smb/data/catalog.hpp"

#include <algorithm>

namespace rl::smb {

namespace {
QString hex(int value) {
    return QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}
}  // namespace

const QVector<SmbObjectKind>& objectCatalog() {
    static const QVector<SmbObjectKind> kEntries{
        {QStringLiteral("Question block (power-up)"), 8, 0x00},
        {QStringLiteral("Question block (coin)"), 8, 0x01},
        {QStringLiteral("Hidden block (coin)"), 8, 0x02},
        {QStringLiteral("Hidden block (1-up)"), 8, 0x03},
        {QStringLiteral("Brick (power-up)"), 8, 0x04},
        {QStringLiteral("Brick (vine)"), 8, 0x05},
        {QStringLiteral("Brick (star)"), 8, 0x06},
        {QStringLiteral("Brick (coins)"), 8, 0x07},
        {QStringLiteral("Brick (1-up)"), 8, 0x08},
        {QStringLiteral("Sideways pipe"), 8, 0x09},
        {QStringLiteral("Used block"), 8, 0x0a},
        {QStringLiteral("Springboard"), 8, 0x0b},
        {QStringLiteral("Tree or mushroom ledge"), 8, 0x10},
        {QStringLiteral("Row of bricks"), 8, 0x20},
        {QStringLiteral("Row of blocks"), 8, 0x30},
        {QStringLiteral("Row of coins"), 8, 0x40},
        {QStringLiteral("Column of bricks"), 8, 0x50},
        {QStringLiteral("Column of blocks"), 8, 0x60},
        {QStringLiteral("Pipe"), 8, 0x70},
        {QStringLiteral("Hole"), 12, 0x00},
        {QStringLiteral("Pulley rope"), 12, 0x10},
        {QStringLiteral("Bridge row 7"), 12, 0x20},
        {QStringLiteral("Bridge row 8"), 12, 0x30},
        {QStringLiteral("Bridge row 10"), 12, 0x40},
        {QStringLiteral("Water or lava hole"), 12, 0x50},
        {QStringLiteral("Question row 3"), 12, 0x60},
        {QStringLiteral("Question row 7"), 12, 0x70},
        {QStringLiteral("Reverse L pipe"), 13, 0x40},
        {QStringLiteral("Flagpole"), 13, 0x41},
        {QStringLiteral("Boss axe"), 13, 0x42},
        {QStringLiteral("Axe rope"), 13, 0x43},
        {QStringLiteral("Boss bridge"), 13, 0x44},
        {QStringLiteral("Warp scroll stop"), 13, 0x45},
        {QStringLiteral("Scroll stop"), 13, 0x46},
        {QStringLiteral("Alternate scroll stop"), 13, 0x47},
        {QStringLiteral("Flying-Fish generator"), 13, 0x48},
        {QStringLiteral("Bullet generator"), 13, 0x49},
        {QStringLiteral("Stop generator"), 13, 0x4a},
        {QStringLiteral("Area loop"), 13, 0x4b},
    };
    return kEntries;
}

// The enemies a level's stream can place. $09 is left out: it has no initializer
// (its sprite is an unused green Flier), so a placed one never appears.
const QVector<SmbEnemyKind>& enemyCatalog() {
    static const QVector<SmbEnemyKind> kEntries{
        {0x00, QStringLiteral("Green Turtle")},
        {0x01, QStringLiteral("Red Turtle")},
        {0x02, QStringLiteral("Beetle")},
        {0x05, QStringLiteral("Hammer Thrower")},
        {0x06, QStringLiteral("Walker")},
        {0x07, QStringLiteral("Squid")},
        {0x08, QStringLiteral("Bullet")},
        {0x0a, QStringLiteral("Green Flying-Fish")},
        {0x0b, QStringLiteral("Red Flying-Fish")},
        {0x0c, QStringLiteral("Bubble")},
        {0x0d, QStringLiteral("Biting Plant")},
        {0x0e, QStringLiteral("Jumping Flier")},
        {0x0f, QStringLiteral("Red Flier")},
        {0x10, QStringLiteral("Flying Flier")},
        {0x11, QStringLiteral("Rider")},
        {0x12, QStringLiteral("Spiker")},
        {0x14, QStringLiteral("Flying Flying-Fish generator")},
        {0x15, QStringLiteral("Boss fire generator")},
        {0x17, QStringLiteral("Bullet and Flying-Fish generator")},
        // Speed and direction come from tbl_firebar_spin_* in the source, by id - $1b.
        {0x1b, QStringLiteral("Slow clockwise firebar")},
        {0x1c, QStringLiteral("Fast clockwise firebar")},
        {0x1d, QStringLiteral("Slow counterclockwise firebar")},
        {0x1e, QStringLiteral("Fast counterclockwise firebar")},
        {0x1f, QStringLiteral("Long firebar")},
        {0x24, QStringLiteral("Balance lift")},
        {0x25, QStringLiteral("Vertical lift")},
        {0x26, QStringLiteral("Large lift up")},
        {0x27, QStringLiteral("Large lift down")},
        {0x28, QStringLiteral("Horizontal lift")},
        {0x29, QStringLiteral("Falling lift")},
        {0x2a, QStringLiteral("Right-moving lift")},
        {0x2b, QStringLiteral("Small lift up")},
        {0x2c, QStringLiteral("Small lift down")},
        {0x2d, QStringLiteral("Boss")},
        {0x35, QStringLiteral("Retainer")},
        // Groups: in the primary hard mode the Walkers are Beetles.
        {0x37, QStringLiteral("Two Walkers")},
        {0x38, QStringLiteral("Three Walkers")},
        {0x39, QStringLiteral("Two Walkers (upper row)")},
        {0x3a, QStringLiteral("Three Walkers (upper row)")},
        {0x3b, QStringLiteral("Two green Turtles")},
        {0x3c, QStringLiteral("Three green Turtles")},
        {0x3d, QStringLiteral("Two green Turtles (upper row)")},
        {0x3e, QStringLiteral("Three green Turtles (upper row)")},
    };
    return kEntries;
}

QString areaObjectName(int row, int control) {
    const QVector<SmbObjectKind>& catalog = objectCatalog();
    if (row <= 11) {
        const int family = (control >> 4) & 7;
        if (family == 0 && (control & 15) < 12) {
            return catalog.at(control & 15).name;
        }
        const int index = 12 + family - 1;
        if (index >= 12 && index < 19) {
            return catalog.at(index).name;
        }
    } else if (row == 12) {
        return catalog.at(19 + ((control >> 4) & 7)).name;
    } else if (row == 13 && (control & 0x40) && (control & 0x3f) < 12) {
        return catalog.at(27 + (control & 0x3f)).name;
    }
    return QStringLiteral("Stream command $%1").arg(hex(control));
}

QString enemyName(int id) {
    const QVector<SmbEnemyKind>& catalog = enemyCatalog();
    const auto found =
        std::find_if(catalog.cbegin(), catalog.cend(), [id](const SmbEnemyKind& kind) { return kind.id == id; });
    return found == catalog.cend() ? QStringLiteral("Enemy $%1").arg(hex(id)) : found->name;
}

}  // namespace rl::smb
