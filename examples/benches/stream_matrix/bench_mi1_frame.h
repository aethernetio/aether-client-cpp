/*
 * Copyright 2026 Aethernet Inc.
 * Fixed 1024-byte P2pStream write frame for 1 MiB TCP benchmark.
 */
#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_MI1_FRAME_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_MI1_FRAME_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "aether/types/data_buffer.h"

namespace ae::stream_matrix_bench {

inline constexpr std::size_t kMi1FrameBytes = 1024;
inline constexpr std::uint32_t kMi1Magic = 0x316D4541u;  // 'AEM1' LE
inline constexpr std::uint64_t kMi1MessageCount = 1024;
inline constexpr double kMi1PeriodMs = 10.0;

#pragma pack(push, 1)
struct Mi1WireHeader {
  std::uint32_t magic{};
  std::uint64_t run_id{};
  std::uint64_t sequence{};
  std::uint32_t frame_bytes{};
  std::uint32_t header_crc32{};
};
#pragma pack(pop)

static_assert(sizeof(Mi1WireHeader) == 28, "mi1 header size");

inline std::uint32_t Mi1PatternByte(std::size_t index, std::uint64_t sequence) {
  return static_cast<std::uint32_t>((index * 131u + sequence * 17u + 0xA5u) & 0xFFu);
}

inline std::uint32_t Crc32Mi1(void const* data, std::size_t len) {
  auto const* p = static_cast<std::uint8_t const*>(data);
  std::uint32_t crc = 0xffffffffu;
  for (std::size_t i = 0; i < len; ++i) {
    crc ^= p[i];
    for (int b = 0; b < 8; ++b) {
      std::uint32_t const mask = (crc & 1u) ? 0xFFFFFFFFu : 0u;
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return ~crc;
}

inline DataBuffer MakeMi1Frame(std::uint64_t run_id, std::uint64_t sequence) {
  std::vector<std::uint8_t> bytes(kMi1FrameBytes);
  Mi1WireHeader hdr{
      .magic = kMi1Magic,
      .run_id = run_id,
      .sequence = sequence,
      .frame_bytes = static_cast<std::uint32_t>(kMi1FrameBytes),
      .header_crc32 = 0,
  };
  hdr.header_crc32 = Crc32Mi1(&hdr, offsetof(Mi1WireHeader, header_crc32));
  std::memcpy(bytes.data(), &hdr, sizeof(hdr));
  for (std::size_t i = sizeof(hdr); i < kMi1FrameBytes; ++i) {
    bytes[i] = static_cast<std::uint8_t>(Mi1PatternByte(i, sequence));
  }
  return DataBuffer{std::move(bytes)};
}

inline std::optional<Mi1WireHeader> ParseMi1Header(DataBuffer const& data) {
  if (data.size() != kMi1FrameBytes) {
    return std::nullopt;
  }
  Mi1WireHeader hdr{};
  std::memcpy(&hdr, data.data(), sizeof(hdr));
  if (hdr.magic != kMi1Magic || hdr.frame_bytes != kMi1FrameBytes) {
    return std::nullopt;
  }
  std::uint32_t const saved = hdr.header_crc32;
  hdr.header_crc32 = 0;
  if (Crc32Mi1(&hdr, offsetof(Mi1WireHeader, header_crc32)) != saved) {
    return std::nullopt;
  }
  hdr.header_crc32 = saved;
  return hdr;
}

inline bool Mi1PayloadMatches(DataBuffer const& data, std::uint64_t sequence) {
  if (data.size() != kMi1FrameBytes) {
    return false;
  }
  for (std::size_t i = sizeof(Mi1WireHeader); i < kMi1FrameBytes; ++i) {
    if (data[i] != static_cast<std::uint8_t>(Mi1PatternByte(i, sequence))) {
      return false;
    }
  }
  return true;
}

inline bool Mi1FrameMatchesExpected(std::uint64_t run_id, std::uint64_t sequence,
                                    DataBuffer const& data) {
  auto hdr = ParseMi1Header(data);
  if (!hdr || hdr->run_id != run_id || hdr->sequence != sequence) {
    return false;
  }
  return Mi1PayloadMatches(data, sequence);
}

}  // namespace ae::stream_matrix_bench

#endif
