#include "megadrive/tiles.hpp"

namespace rl::megadrive {

Tile decode4bppTile(const Packed4bppTile& raw) {
    Tile tile{};
    for (std::size_t index = 0; index < raw.size(); ++index) {
        tile[index * 2] = raw[index] >> 4;
        tile[index * 2 + 1] = raw[index] & 0x0f;
    }
    return tile;
}

Packed4bppTile encode4bppTile(const Tile& tile) {
    Packed4bppTile raw{};
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const auto left = tile[index * 2];
        const auto right = tile[index * 2 + 1];
        if (left > 15 || right > 15) {
            throw TileError("4bpp tile pixels must be 0..15");
        }
        raw[index] = static_cast<std::uint8_t>((left << 4) | right);
    }
    return raw;
}

Tile decode1bppTile(const Packed1bppTile& raw) {
    Tile tile{};
    for (std::size_t row = 0; row < raw.size(); ++row) {
        for (std::size_t column = 0; column < 8; ++column) {
            tile[row * 8 + column] = static_cast<std::uint8_t>((raw[row] >> (7 - column)) & 1);
        }
    }
    return tile;
}

Packed1bppTile encode1bppTile(const Tile& tile) {
    Packed1bppTile raw{};
    for (std::size_t row = 0; row < raw.size(); ++row) {
        for (std::size_t column = 0; column < 8; ++column) {
            const auto pixel = tile[row * 8 + column];
            if (pixel > 1) {
                throw TileError("1bpp tile pixels must be 0 or 1");
            }
            raw[row] = static_cast<std::uint8_t>((raw[row] << 1) | pixel);
        }
    }
    return raw;
}

}  // namespace rl::megadrive
