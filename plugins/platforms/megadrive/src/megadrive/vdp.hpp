#pragma once

#include <cstdint>

// Mega Drive VDP color and name-table conventions. Qt-free.
namespace rl::megadrive {

// A CRAM word is 0000 BBB0 GGG0 RRR0: three bits per channel, bit 0 of each
// nibble unused.
inline constexpr int kCramChannelMask = 0x0EEE;

// 0xAARRGGBB with the channel levels the VDP produces in normal (non
// shadow/highlight) mode: level * 0x24.
[[nodiscard]] constexpr std::uint32_t cramToArgb(int word) {
    const auto level = [](int value) { return static_cast<std::uint32_t>((value & 7) * 0x24); };
    return 0xFF000000u | (level(word >> 1) << 16) | (level(word >> 5) << 8) | level(word >> 9);
}

[[nodiscard]] constexpr int cramRed(int word) { return (word >> 1) & 7; }
[[nodiscard]] constexpr int cramGreen(int word) { return (word >> 5) & 7; }
[[nodiscard]] constexpr int cramBlue(int word) { return (word >> 9) & 7; }
[[nodiscard]] constexpr int cramWord(int red, int green, int blue) {
    return ((red & 7) << 1) | ((green & 7) << 5) | ((blue & 7) << 9);
}

// A plane name-table word: priority, palette line, flips and pattern index.
struct TileWord {
    int pattern = 0;       // bits 0-10
    bool hflip = false;    // bit 11
    bool vflip = false;    // bit 12
    int palette = 0;       // bits 13-14
    bool priority = false; // bit 15
};

[[nodiscard]] constexpr TileWord decodeTileWord(int word) {
    TileWord result;
    result.pattern = word & 0x07FF;
    result.hflip = (word & 0x0800) != 0;
    result.vflip = (word & 0x1000) != 0;
    result.palette = (word >> 13) & 3;
    result.priority = (word & 0x8000) != 0;
    return result;
}

[[nodiscard]] constexpr int encodeTileWord(const TileWord& tile) {
    return (tile.pattern & 0x07FF) | (tile.hflip ? 0x0800 : 0) | (tile.vflip ? 0x1000 : 0) |
           ((tile.palette & 3) << 13) | (tile.priority ? 0x8000 : 0);
}

}  // namespace rl::megadrive
