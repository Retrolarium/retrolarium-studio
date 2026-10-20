#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <array>

namespace rl::rom::tiles {

// An 8x8 tile layout as consoles store them:
//   1bpp   one byte a row, bit 7 the left pixel
//   nes    two planes of eight bytes (NES, Famicom)
//   gb     the two planes interleaved row by row (Game Boy, SNES 2bpp)
//   snes4  two interleaved plane pairs (SNES, PC Engine 4bpp)
//   md     four bytes a row, the high nibble the left pixel (Mega Drive)
//   gba    four bytes a row, the low nibble the left pixel (Game Boy Advance)
//   8bpp   one byte a pixel
enum class Layout { OneBit, Nes, GameBoy, Snes4, MegaDrive, Gba, EightBit };

struct Format {
    QString id;
    Layout layout = Layout::Nes;
    int bits = 2;
    int bytes = 16;  // a tile
};

using Pixels = std::array<quint8, 64>;

const QVector<Format>& formats();
// The format with this id; the NES one when there is none.
const Format& format(const QString& id);

Pixels decode(const Format& format, const char* data);
void encode(const Format& format, const Pixels& pixels, char* data);

// How a tile reads in a format: one colour all over, like a picture (its
// neighbouring pixels agree far more often than random bytes would make
// them), or noise.
enum class Look { Empty, Picture, Noise };
Look look(const Format& format, const char* data);

// A run of tiles that look like pictures, from `offset` in the file;
// `pictures` of its `tiles` read as pictures, the rest are empty or noisy.
struct Section {
    int offset = 0;
    int tiles = 0;
    int pictures = 0;
};
QVector<Section> sections(const QByteArray& bytes, int from, const Format& format);

// The format the bytes from `from` on most look like tiles in: the one whose
// sections cover the most of them. Empty when hardly anything reads as tiles
// (the graphics are packed or compressed, or the file holds none).
QString detect(const QByteArray& bytes, int from);

}  // namespace rl::rom::tiles
