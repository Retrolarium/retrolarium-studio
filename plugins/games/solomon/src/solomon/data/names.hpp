#pragma once

#include <QString>

namespace rl::solomon {

// What the game shows for a RoomMap identity (item type & $3F), from the
// source reconstruction's element catalog.
QString itemName(int identity);
// An item type byte: its identity's name and the hidden and in-block bits.
QString itemTypeName(int type);
// An enemy type ($18-$83): family, direction and speed as the game reads
// them from the type's bits.
QString enemyName(int type);
// The constellation an item-list opcode $F0-$FB draws.
QString constellationName(int opcode);

}  // namespace rl::solomon
