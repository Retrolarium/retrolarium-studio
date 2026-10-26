#pragma once

#include <QByteArray>

// The size of the payload the kit's LZSS encoder (lzss.py) writes for a
// stream only the RAM loaders read, a stage or screen map: the same optimal
// parse over the whole stream, counted instead of written, so an edit can be
// checked against the map's slot while it is made. The kit compresses; this
// only measures.
namespace rl::alien_soldier {

// Header word, stream and the pad byte of an odd payload.
int lzssPayloadSize(const QByteArray& data);

}  // namespace rl::alien_soldier
