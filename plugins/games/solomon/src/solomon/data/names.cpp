#include "solomon/data/names.hpp"

#include <QStringList>

namespace rl::solomon {

namespace {

const char* const kItems[64] = {
    "Brown block (glitch)",
    "Broken block (glitch)",
    "Door (glitch)",
    "White block (glitch)",
    "Bat symbol",
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
    "Blue jewels / blue Archdemon",
    "Shrine 1",
    "Shrine 2",
    "Shrine 3",
    "Shrine 4",
    "Royal Seal",
    "Solomon page / Egyptian head",
    "Golden Wings / warp item",
    "Open door (out-of-table glitch 1)",
    "Open door (out-of-table glitch 2)",
    "Silver coin",
    "Silver double coin",
    "Blue opal",
    "Gold coin",
    "Gold double coin",
    "Orange opal",
    "Star coin",
    "Double star coin",
    "Dark orange opal",
    "Origami swan",
    "Hornhead coin",
    "Sphinx",
    "Egyptian head",
    "Magic lamp / Bonus Rabbit reward",
    "E-bottle",
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
    if (type & 0x80) flags << QStringLiteral("in a brown block");
    return flags.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, flags.join(QStringLiteral(", ")));
}

QString constellationName(int opcode) {
    return opcode >= 0xf0 && opcode <= 0xfb ? QString::fromLatin1(kConstellations[opcode - 0xf0]) : QString();
}

QString enemyName(int type) {
    if (type < 0x18 || type > 0x83) return QStringLiteral("Not an enemy");
    if (type < 0x1c) {
        const int variant = type - 0x18;
        return variant ? QStringLiteral("Medal Hero variant %1").arg(variant + 1) : QStringLiteral("Medal Hero");
    }
    if (type < 0x20) {
        static const char* const kFairies[4] = {"Fairy", "Captive Fairy", "Erratic Fairy (glitch)",
                                                "Erratic Captive Fairy (glitch)"};
        return QString::fromLatin1(kFairies[type - 0x1c]);
    }
    if (type < 0x24) return QStringLiteral("Bullet (%1)").arg(direction4(type - 0x20));
    if (type < 0x28) return QStringLiteral("Wall Face (%1)").arg(direction4(type - 0x24));
    if (type < 0x30) {
        static const char* const kSpins[4] = {"right, counterclockwise", "left, clockwise", "up, clockwise",
                                              "down, counterclockwise"};
        const int offset = type - 0x28;
        return QStringLiteral("Fireball (%1, speed %2)").arg(QString::fromLatin1(kSpins[offset & 3])).arg(offset / 4 + 1);
    }
    if (type < 0x50) {
        const int offset = type - 0x30;
        const int group = offset / 4;
        const int variant = offset & 3;
        const bool neul = group % 2 == 0;
        const QString direction = neul ? (variant < 2 ? QStringLiteral("up") : QStringLiteral("down"))
                                       : (variant < 2 ? QStringLiteral("right") : QStringLiteral("left"));
        return QStringLiteral("%1 (%2, speed %3%4)%5")
            .arg(neul ? QStringLiteral("Neul") : QStringLiteral("Ghost"), direction)
            .arg(group / 2 % 2 + 1)
            .arg(group >= 4 ? QStringLiteral(", no slow-down") : QString())
            .arg(variant & 1 ? QStringLiteral(" variant 2") : QString());
    }
    QString family;
    int base = 0;
    if (type < 0x5c) { family = QStringLiteral("Hornhead"); base = 0x50; }
    else if (type < 0x68) { family = QStringLiteral("Saramandor"); base = 0x5c; }
    else if (type < 0x70) { family = QStringLiteral("Dragon"); base = 0x68; }
    else if (type < 0x78) { family = QStringLiteral("Golem"); base = 0x70; }
    else if (type < 0x80) { family = QStringLiteral("Gargoyle"); base = 0x78; }
    else {
        static const char* const kFlames[4] = {"Red flame", "White flame", "Red flame variant 2", "White flame variant 2"};
        return QString::fromLatin1(kFlames[type - 0x80]);
    }
    const int offset = type - base;
    const int variant = offset & 3;
    return QStringLiteral("%1 (%2, speed %3)%4")
        .arg(family, variant == 0 || variant == 2 ? QStringLiteral("right") : QStringLiteral("left"))
        .arg(offset / 4 + 1)
        .arg(variant >= 2 ? QStringLiteral(" variant 2") : QString());
}

}  // namespace rl::solomon
