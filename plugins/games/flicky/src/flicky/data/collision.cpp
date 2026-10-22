#include "flicky/data/collision.hpp"

#include <algorithm>

namespace rl::flicky {
namespace {

constexpr int streamStart = 0x40;

[[noreturn]] void fail(const char* message) { throw FlickyCollisionError(message); }

}  // namespace

FlickyCollisionGrid decodeFlickyCollision(const std::vector<std::uint8_t>& stream) {
    FlickyCollisionGrid grid{};
    int cursor = streamStart;
    for (std::size_t index = 0; index < stream.size(); ++index) {
        const int command = stream[index];
        if (command == 0) {
            if (index + 1 != stream.size()) {
                fail("collision stream contains data after its terminator");
            }
            return grid;
        }
        if (command & 0x80) {
            const int length = command & 0x3f;
            if (!length) {
                fail("collision stream contains a zero-length run");
            }
            if (command & 0x40) {
                // Vertical run advances the stream cursor by one column only.
                if (cursor + (length - 1) * flickyMapWidth >= flickyMapSize) {
                    fail("collision run leaves the 32x28 map");
                }
                for (int step = 0; step < length; ++step) {
                    grid[static_cast<std::size_t>(cursor + step * flickyMapWidth)] = 1;
                }
                ++cursor;
            } else {
                if (cursor + length > flickyMapSize) {
                    fail("collision run leaves the 32x28 map");
                }
                std::fill_n(grid.begin() + cursor, length, std::uint8_t{1});
                cursor += length;
            }
        } else {
            cursor += command;
            if (cursor > flickyMapSize) {
                fail("collision skip leaves the 32x28 map");
            }
        }
    }
    fail("collision stream has no terminator");
}

std::vector<std::uint8_t> encodeFlickyCollision(const FlickyCollisionGrid& grid) {
    if (std::any_of(grid.begin(), grid.begin() + streamStart, [](std::uint8_t cell) { return cell != 0; })) {
        fail("the first two collision rows are generated at runtime");
    }
    if (std::any_of(grid.begin(), grid.end(), [](std::uint8_t cell) { return cell > 1; })) {
        fail("collision cells must be 0 or 1");
    }
    std::vector<std::uint8_t> output;
    int cursor = streamStart;
    for (int row = streamStart / flickyMapWidth; row < flickyMapHeight; ++row) {
        int column = 0;
        while (column < flickyMapWidth) {
            const int position = row * flickyMapWidth + column;
            if (!grid[static_cast<std::size_t>(position)]) {
                ++column;
                continue;
            }
            while (cursor < position) {
                const int distance = std::min(position - cursor, 0x7f);
                output.push_back(static_cast<std::uint8_t>(distance));
                cursor += distance;
            }
            int length = 0;
            while (column + length < flickyMapWidth && grid[static_cast<std::size_t>(position + length)] &&
                   length < 0x3f) {
                ++length;
            }
            output.push_back(static_cast<std::uint8_t>(0x80 | length));
            cursor += length;
            column += length;
        }
    }
    output.push_back(0);
    return output;
}

}  // namespace rl::flicky
