#include "solomon/data/names.hpp"

#include "sdk/game_names.hpp"
#include "sdk/i18n.hpp"

#include <QStringList>

namespace rl::solomon {

namespace {

QString hex(int value) { return QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper(); }

QString direction4(int variant) {
    static const QStringList kDirections{QStringLiteral("right"), QStringLiteral("left"), QStringLiteral("up"),
                                         QStringLiteral("down")};
    return kDirections.at(variant & 3);
}

// An enemy family by its first type, or one type, as the game names it.
QString enemyKind(int type) { return sdk::gameName("enemies", type, rlTr("solomon", "Enemy $%1").arg(hex(type))); }

}  // namespace

QString itemName(int identity) {
    const int id = identity & 0x3f;
    return sdk::gameName("items", id, rlTr("solomon", "Item $%1").arg(hex(id)));
}

QString itemTypeName(int type) {
    QString name = itemName(type);
    if (type >= 0xc0) return name;
    QStringList flags;
    if (type & 0x40) flags << QStringLiteral("hidden");
    if (type & 0x80) flags << rlTr("solomon", "in a brown block");
    return flags.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, flags.join(QStringLiteral(", ")));
}

QString constellationName(int opcode) {
    if (opcode < 0xf0 || opcode > 0xfb) return QString();
    return sdk::gameName("constellations", opcode, rlTr("solomon", "Constellation $%1").arg(hex(opcode)));
}

QString enemyFamilyName(int type) { return enemyKind(type); }

QString enemyName(int type) {
    if (type < 0x18 || type > 0x83) return rlTr("solomon", "Not an enemy");
    if (type < 0x1c) {
        const int variant = type - 0x18;
        return variant ? rlTr("solomon", "%1 variant %2").arg(enemyKind(0x18)).arg(variant + 1) : enemyKind(0x18);
    }
    if (type < 0x20) return enemyKind(type);
    if (type < 0x24) return rlTr("solomon", "%1 (%2)").arg(enemyKind(0x20), direction4(type - 0x20));
    if (type < 0x28) return rlTr("solomon", "%1 (%2)").arg(enemyKind(0x24), direction4(type - 0x24));
    if (type < 0x30) {
        static const char* const kSpins[4] = {"right, counterclockwise", "left, clockwise", "up, clockwise",
                                              "down, counterclockwise"};
        const int offset = type - 0x28;
        return rlTr("solomon", "%1 (%2, speed %3)")
            .arg(enemyKind(0x28), QString::fromLatin1(kSpins[offset & 3]))
            .arg(offset / 4 + 1);
    }
    if (type < 0x50) {
        // Even groups of four fly up and down, odd ones left and right.
        const int offset = type - 0x30;
        const int group = offset / 4;
        const int variant = offset & 3;
        const bool vertical = group % 2 == 0;
        const QString direction = vertical ? (variant < 2 ? QStringLiteral("up") : QStringLiteral("down"))
                                           : (variant < 2 ? QStringLiteral("right") : QStringLiteral("left"));
        return rlTr("solomon", "%1 (%2, speed %3%4)%5")
            .arg(enemyKind(vertical ? 0x30 : 0x34), direction)
            .arg(group / 2 % 2 + 1)
            .arg(group >= 4 ? rlTr("solomon", ", no slow-down") : QString())
            .arg(variant & 1 ? rlTr("solomon", " variant 2") : QString());
    }
    if (type >= 0x80) return enemyKind(type);
    // $74-$77 run the $70 family's code with other tiles (CHR bank 2 draws
    // them differently).
    const int base = type < 0x5c   ? 0x50
                     : type < 0x68 ? 0x5c
                     : type < 0x70 ? 0x68
                     : type < 0x74 ? 0x70
                     : type < 0x78 ? 0x74
                                   : 0x78;
    const int offset = type - base;
    const int variant = offset & 3;
    return rlTr("solomon", "%1 (%2, speed %3)%4")
        .arg(enemyKind(base), variant == 0 || variant == 2 ? QStringLiteral("right") : QStringLiteral("left"))
        .arg(offset / 4 + 1)
        .arg(variant >= 2 ? rlTr("solomon", " variant 2") : QString());
}

QString playerName() { return sdk::gameName("player", 0, rlTr("solomon", "Player")); }

}  // namespace rl::solomon
