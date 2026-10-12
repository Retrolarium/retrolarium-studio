#pragma once

#include <QString>

namespace rl::solomon {

// The names are the game's (its repository's config/names.json); where one
// is missing the numbers stand in ("Item $20", "Enemy $50").
//
// What a RoomMap identity (item type & $3F) is.
QString itemName(int identity);
// An item type byte: its identity's name and the hidden and in-block bits.
QString itemTypeName(int type);
// An enemy type ($18-$83): family, direction and speed as the game reads
// them from the type's bits.
QString enemyName(int type);
// An enemy family by its first type ($50), without direction and speed.
QString enemyFamilyName(int type);
// The player character.
QString playerName();
// The constellation an item-list opcode $F0-$FB draws.
QString constellationName(int opcode);

}  // namespace rl::solomon
