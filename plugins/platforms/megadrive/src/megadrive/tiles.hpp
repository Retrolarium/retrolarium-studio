#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>

// Mega Drive VDP pattern formats. Qt-free; shared by every Mega Drive game.
namespace rl::megadrive {

class TileError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// An 8x8 pattern as one color index per pixel, row-major.
using Tile = std::array<std::uint8_t, 64>;
using Packed4bppTile = std::array<std::uint8_t, 32>;
using Packed1bppTile = std::array<std::uint8_t, 8>;

// 4bpp VDP pattern: high nibble is the left pixel.
[[nodiscard]] Tile decode4bppTile(const Packed4bppTile& raw);
[[nodiscard]] Packed4bppTile encode4bppTile(const Tile& tile);

// 1bpp font pattern: most significant bit is the left pixel.
[[nodiscard]] Tile decode1bppTile(const Packed1bppTile& raw);
[[nodiscard]] Packed1bppTile encode1bppTile(const Tile& tile);

}  // namespace rl::megadrive
