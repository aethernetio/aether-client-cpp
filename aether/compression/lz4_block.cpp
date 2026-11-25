#include "aether/compression/lz4_block.h"

#include <limits>
#include <lz4.h>

namespace ae::compression {
bool CompressLz4Block(std::span<const std::uint8_t> input,
                      std::vector<std::uint8_t>& output) {
  if (input.size() > LZ4_MAX_INPUT_SIZE) return false;
  auto size = static_cast<int>(input.size());
  auto capacity = LZ4_compressBound(size);
  const char empty{};
  output.resize(capacity);
  auto written = LZ4_compress_default(
      input.empty() ? &empty : reinterpret_cast<const char*>(input.data()),
      reinterpret_cast<char*>(output.data()), size, capacity);
  if (written <= 0) {
    output.clear();
    return false;
  }
  output.resize(written);
  return true;
}

bool ExpandLz4Block(std::span<const std::uint8_t> input,
                    std::span<std::uint8_t> output) {
  if (input.empty() || input.size() > std::numeric_limits<int>::max() ||
      output.size() > std::numeric_limits<int>::max()) return false;
  char empty{};
  auto size = static_cast<int>(output.size());
  return LZ4_decompress_safe(
             reinterpret_cast<const char*>(input.data()),
             output.empty() ? &empty : reinterpret_cast<char*>(output.data()),
             static_cast<int>(input.size()), size) == size;
}
}  // namespace ae::compression
