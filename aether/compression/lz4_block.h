#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace ae::compression {
// A raw LZ4 block. The owning format stores its lengths and decides whether
// to keep the original bytes when compression does not make a block smaller.
bool CompressLz4Block(std::span<const std::uint8_t> input,
                      std::vector<std::uint8_t>& output);

// The caller supplies the expected decoded size. Invalid input or a different
// decoded size reports failure. The output span can be modified on failure.
bool ExpandLz4Block(std::span<const std::uint8_t> input,
                    std::span<std::uint8_t> output);
}  // namespace ae::compression
