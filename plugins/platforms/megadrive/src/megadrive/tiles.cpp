#include <retrolarium/flicky_tiles.hpp>

namespace retrolarium {

FlickyTile decodeFlicky4bppTile(const FlickyPacked4bppTile &raw)
{
    FlickyTile tile {};
    for (std::size_t index = 0; index < raw.size(); ++index) {
        tile[index * 2] = raw[index] >> 4;
        tile[index * 2 + 1] = raw[index] & 0x0f;
    }
    return tile;
}

FlickyPacked4bppTile encodeFlicky4bppTile(const FlickyTile &tile)
{
    FlickyPacked4bppTile raw {};
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const auto left = tile[index * 2];
        const auto right = tile[index * 2 + 1];
        if (left > 15 || right > 15) {
            throw FlickyTileError("4bpp tile pixels must be 0..15");
        }
        raw[index] = static_cast<std::uint8_t>((left << 4) | right);
    }
    return raw;
}

FlickyTile decodeFlicky1bppTile(const FlickyPacked1bppTile &raw)
{
    FlickyTile tile {};
    for (std::size_t row = 0; row < raw.size(); ++row) {
        for (std::size_t column = 0; column < 8; ++column) {
            tile[row * 8 + column] =
                static_cast<std::uint8_t>((raw[row] >> (7 - column)) & 1);
        }
    }
    return tile;
}

FlickyPacked1bppTile encodeFlicky1bppTile(const FlickyTile &tile)
{
    FlickyPacked1bppTile raw {};
    for (std::size_t row = 0; row < raw.size(); ++row) {
        for (std::size_t column = 0; column < 8; ++column) {
            const auto pixel = tile[row * 8 + column];
            if (pixel > 1) {
                throw FlickyTileError("1bpp tile pixels must be 0 or 1");
            }
            raw[row] = static_cast<std::uint8_t>((raw[row] << 1) | pixel);
        }
    }
    return raw;
}

} // namespace retrolarium
