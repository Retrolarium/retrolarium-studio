#include "solomon/data/names.hpp"

#include "sdk/i18n.hpp"

#include <QStringList>

namespace rl::solomon {

namespace {

const char* const kItems[64] = {
    "Brown block (glitch)",
    "Broken block (glitch)",
    "Door (glitch)",
    "White block (glitch)",
    "Dark Emblem",
    "Spawn Mirror",
    "Key (glitch)",
    "Open door (glitch)",
    "Blue diamond",
    "Blue fire jar",
    "Gold double coin",
    "Orange jewels / red Archdemon",
    "Orange diamond",
    "Orange fire jar",
    "Scroll",
    "Bell",
    "Nothing",
    "Half time bottle",
    "Full time bottle",
    "Blue hourglass",
    "Orange hourglass",
    "Blue fire jar",
    "Orange fire jar",
    "Scroll",
    "Bell",
    "Explosion jar",
    "Blue key",
    "Blue jewel (blue Archdemon)",
    "Zodiac panel (fire signs)",
    "Zodiac panel (earth signs)",
    "Zodiac panel (air signs)",
    "Zodiac panel (water signs)",
    "Royal Seal",
    "Solomon page / Egyptian head",
    "Golden Wings",
    "Open door (out-of-table glitch 1)",
    "Open door (out-of-table glitch 2)",
    "Silver coin",
    "Two silver coins",
    "Framed blue jewel",
    "Gold coin",
    "Two gold coins",
    "Framed gold jewel",
    "Gold star coin",
    "Two gold star coins",
    "Gold-framed gold jewel",
    "Golden crane",
    "Medal Hero medal",
    "Sphinx",
    "Pharaoh head",
    "Magic lamp",
    "Extra life",
    "Extra life (glitch 1)",
    "Extra life (glitch 2)",
    "Extra life (glitch 3)",
    "Mini-Wizard",
    "White Bonus Rabbit",
    "Orange Bonus Rabbit",
    "Unclassified graphics 1",
    "Unclassified graphics 2",
    "Unclassified graphics 3",
    "Unclassified graphics 4",
    "Unclassified graphics 5",
    "Unclassified graphics 6",
};

const char* const kConstellations[12] = {"Aries",   "Gemini", "Virgo", "Aquarius", "Cancer", "Scorpio",
                                         "Capricorn", "Pisces", "Taurus", "Leo",     "Libra",  "Sagittarius"};

QString direction4(int variant) {
    static const QStringList kDirections{QStringLiteral("right"), QStringLiteral("left"), QStringLiteral("up"),
                                         QStringLiteral("down")};
    return kDirections.at(variant & 3);
}

}  // namespace

QString itemName(int identity) {
    return QString::fromLatin1(kItems[identity & 0x3f]);
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
    return opcode >= 0xf0 && opcode <= 0xfb ? QString::fromLatin1(kConstellations[opcode - 0xf0]) : QString();
}

QString enemyName(int type) {
    if (type < 0x18 || type > 0x83) return rlTr("solomon", "Not an enemy");
    if (type < 0x1c) {
        const int variant = type - 0x18;
        return variant ? rlTr("solomon", "Medal Hero variant %1").arg(variant + 1) : rlTr("solomon", "Medal Hero");
    }
    if (type < 0x20) {
        static const char* const kFairies[4] = {"Fairy", "Captive Fairy", "Erratic Fairy (glitch)",
                                                "Erratic Captive Fairy (glitch)"};
        return QString::fromLatin1(kFairies[type - 0x1c]);
    }
    if (type < 0x24) return rlTr("solomon", "Bullet (%1)").arg(direction4(type - 0x20));
    if (type < 0x28) return rlTr("solomon", "Wall Face (%1)").arg(direction4(type - 0x24));
    if (type < 0x30) {
        static const char* const kSpins[4] = {"right, counterclockwise", "left, clockwise", "up, clockwise",
                                              "down, counterclockwise"};
        const int offset = type - 0x28;
        return rlTr("solomon", "Spark ball (%1, speed %2)").arg(QString::fromLatin1(kSpins[offset & 3])).arg(offset / 4 + 1);
    }
    if (type < 0x50) {
        const int offset = type - 0x30;
        const int group = offset / 4;
        const int variant = offset & 3;
        const bool neul = group % 2 == 0;
        const QString direction = neul ? (variant < 2 ? QStringLiteral("up") : QStringLiteral("down"))
                                       : (variant < 2 ? QStringLiteral("right") : QStringLiteral("left"));
        return rlTr("solomon", "%1 (%2, speed %3%4)%5")
            .arg(neul ? rlTr("solomon", "Wisp") : rlTr("solomon", "Ghost"), direction)
            .arg(group / 2 % 2 + 1)
            .arg(group >= 4 ? rlTr("solomon", ", no slow-down") : QString())
            .arg(variant & 1 ? rlTr("solomon", " variant 2") : QString());
    }
    QString family;
    int base = 0;
    if (type < 0x5c) { family = rlTr("solomon", "Hornhead"); base = 0x50; }
    else if (type < 0x68) { family = rlTr("solomon", "Salamander"); base = 0x5c; }
    else if (type < 0x70) { family = rlTr("solomon", "Dragon"); base = 0x68; }
    // $74-$77 run the same Goblin code with other tiles, which CHR bank 2 draws as an Stone Mage.
    else if (type < 0x74) { family = rlTr("solomon", "Goblin"); base = 0x70; }
    else if (type < 0x78) { family = rlTr("solomon", "Goblin / Stone Mage"); base = 0x74; }
    else if (type < 0x80) { family = rlTr("solomon", "Gargoyle"); base = 0x78; }
    else {
        static const char* const kFlames[4] = {"Red flame", "White flame", "Red flame variant 2", "White flame variant 2"};
        return QString::fromLatin1(kFlames[type - 0x80]);
    }
    const int offset = type - base;
    const int variant = offset & 3;
    return rlTr("solomon", "%1 (%2, speed %3)%4")
        .arg(family, variant == 0 || variant == 2 ? QStringLiteral("right") : QStringLiteral("left"))
        .arg(offset / 4 + 1)
        .arg(variant >= 2 ? rlTr("solomon", " variant 2") : QString());
}

}  // namespace rl::solomon
