#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Mega Drive compression formats, backed by retrolarium_core's codecs (the
// same ones the kit ships), behind a plugin-sized interface.
namespace rl::megadrive {

// Enigma-compresses plane name-table words (big-endian in the stream).
[[nodiscard]] std::vector<std::uint8_t> enigmaCompress(const std::vector<std::uint16_t>& words);

// Size in bytes of the Enigma stream for `words`.
[[nodiscard]] std::size_t enigmaCompressedSize(const std::vector<std::uint16_t>& words);

}  // namespace rl::megadrive
