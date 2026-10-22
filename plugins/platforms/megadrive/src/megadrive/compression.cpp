#include "megadrive/compression.hpp"

#include <retrolarium/core/megadrive/enigma.hpp>

namespace rl::megadrive {

std::vector<std::uint8_t> enigmaCompress(const std::vector<std::uint16_t>& words) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(words.size() * 2);
    for (std::uint16_t word : words) {
        bytes.push_back(static_cast<std::uint8_t>(word >> 8));
        bytes.push_back(static_cast<std::uint8_t>(word & 0xFF));
    }
    return retrolarium::core::enigmaCompress(bytes);
}

std::size_t enigmaCompressedSize(const std::vector<std::uint16_t>& words) { return enigmaCompress(words).size(); }

}  // namespace rl::megadrive
