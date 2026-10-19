/*
 * Copyright 2026 Aethernet Inc.
 * 1 MiB throughput benchmark frames (variable chunk size).
 */
#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_THROUGHPUT_FRAME_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_THROUGHPUT_FRAME_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "aether/types/data_buffer.h"

#include "stream_matrix/bench_limits.h"

namespace ae::stream_matrix_bench {

inline constexpr std::size_t kThroughputTotalBytes = 1048576;
inline constexpr std::size_t kUdpFourBlockTotalBytes = 4096;
inline constexpr std::uint32_t kThptMagic = 0x54504854u;  // 'THPT' LE

#pragma pack(push, 1)
struct ThptWireHeader {
  std::uint32_t magic{};
  std::uint64_t run_id{};
  std::uint64_t chunk_index{};
  std::uint64_t byte_offset{};
  std::uint32_t payload_len{};
  std::uint32_t header_crc32{};
};
#pragma pack(pop)

static_assert(sizeof(ThptWireHeader) == 36, "thpt header size");

inline std::uint32_t ThptPatternByte(std::size_t absolute_index) {
  return static_cast<std::uint32_t>((absolute_index * 131u + 0x7Bu) & 0xFFu);
}

inline std::uint32_t Crc32Thpt(void const* data, std::size_t len) {
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

inline std::vector<std::uint8_t> BuildThroughputBlob() {
  std::vector<std::uint8_t> blob(kThroughputTotalBytes);
  for (std::size_t i = 0; i < blob.size(); ++i) {
    blob[i] = static_cast<std::uint8_t>(ThptPatternByte(i));
  }
  return blob;
}

inline DataBuffer MakeThptChunk(std::vector<std::uint8_t> const& blob,
                                std::uint64_t run_id, std::uint64_t chunk_index,
                                std::uint64_t byte_offset, std::size_t payload_len) {
  std::vector<std::uint8_t> bytes(sizeof(ThptWireHeader) + payload_len);
  ThptWireHeader hdr{
      .magic = kThptMagic,
      .run_id = run_id,
      .chunk_index = chunk_index,
      .byte_offset = byte_offset,
      .payload_len = static_cast<std::uint32_t>(payload_len),
      .header_crc32 = 0,
  };
  hdr.header_crc32 = Crc32Thpt(&hdr, offsetof(ThptWireHeader, header_crc32));
  std::memcpy(bytes.data(), &hdr, sizeof(hdr));
  std::memcpy(bytes.data() + sizeof(hdr), blob.data() + byte_offset, payload_len);
  return DataBuffer{std::move(bytes)};
}

inline std::optional<ThptWireHeader> ParseThptHeader(DataBuffer const& data) {
  if (data.size() < sizeof(ThptWireHeader)) {
    return std::nullopt;
  }
  ThptWireHeader hdr{};
  std::memcpy(&hdr, data.data(), sizeof(hdr));
  if (hdr.magic != kThptMagic) {
    return std::nullopt;
  }
  std::uint32_t const saved = hdr.header_crc32;
  hdr.header_crc32 = 0;
  if (Crc32Thpt(&hdr, offsetof(ThptWireHeader, header_crc32)) != saved) {
    return std::nullopt;
  }
  hdr.header_crc32 = saved;
  if (data.size() != sizeof(ThptWireHeader) + hdr.payload_len) {
    return std::nullopt;
  }
  return hdr;
}

inline bool VerifyThptChunk(DataBuffer const& data, std::vector<std::uint8_t> const& blob,
                            std::uint64_t run_id) {
  auto hdr = ParseThptHeader(data);
  if (!hdr || hdr->run_id != run_id) {
    return false;
  }
  if (hdr->byte_offset + hdr->payload_len > blob.size()) {
    return false;
  }
  auto const* payload = data.data() + sizeof(ThptWireHeader);
  for (std::uint32_t i = 0; i < hdr->payload_len; ++i) {
    if (payload[i] != blob[hdr->byte_offset + i]) {
      return false;
    }
  }
  return true;
}

inline std::size_t MaxTcpWireChunkBytes(StreamInfo const& info) {
  std::size_t cap = kServerMsgMaxBytesSourceDefault;
  if (info.max_element_size > 0) {
    cap = std::min(cap, info.max_element_size);
  }
  return cap;
}

inline std::size_t MaxTcpPayloadChunkBytes(StreamInfo const& info) {
  auto const wire = MaxTcpWireChunkBytes(info);
  if (wire <= sizeof(ThptWireHeader)) {
    return 0;
  }
  return wire - sizeof(ThptWireHeader);
}

inline std::vector<std::size_t> DefaultTcpChunkMatrix(StreamInfo const& info) {
  std::vector<std::size_t> sizes = {1024,  2048,  4096,  8192,
                                    16384, 32768};
  auto const max_payload = MaxTcpPayloadChunkBytes(info);
  if (max_payload > 0 &&
      (sizes.empty() || sizes.back() != max_payload)) {
    sizes.push_back(max_payload);
  }
  sizes.erase(std::remove_if(sizes.begin(), sizes.end(),
                             [&](std::size_t s) { return s > max_payload; }),
              sizes.end());
  return sizes;
}

inline std::vector<std::size_t> DefaultSafeChunkMatrix(
    std::size_t max_payload) {
  std::vector<std::size_t> sizes = {1024, 2048, 4096, 8192, 16384};
  if (max_payload > 0 &&
      (sizes.empty() || sizes.back() != max_payload)) {
    sizes.push_back(max_payload);
  }
  sizes.erase(std::remove_if(sizes.begin(), sizes.end(),
                             [&](std::size_t s) { return s > max_payload; }),
              sizes.end());
  return sizes;
}

inline std::vector<std::size_t> DefaultOutstandingMatrix() {
  return {1, 4, 16, 32, 64};
}

inline std::vector<std::size_t> DefaultHighOutstandingMatrix() {
  return {128, 256, 512};
}

}  // namespace ae::stream_matrix_bench

#endif
