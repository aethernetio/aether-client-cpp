/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef EXAMPLES_BENCHES_AETHER_UAP_DELIVERY_TIMING_BENCH_COMMON_BENCH_MESSAGE_H_
#define EXAMPLES_BENCHES_AETHER_UAP_DELIVERY_TIMING_BENCH_COMMON_BENCH_MESSAGE_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace ae::bench::uap {

inline constexpr std::uint32_t kBenchMagic = 0x41555031u;  // 'AUP1'
inline constexpr std::uint8_t kBenchVersion = 1;

#pragma pack(push, 1)
struct DeliveryBenchMessage {
  std::uint32_t magic{kBenchMagic};
  std::uint8_t version{kBenchVersion};
  std::uint8_t reserved{0};
  std::uint16_t offset_ms{0};
  std::uint32_t sequence{0};
  std::uint64_t send_qpc{0};
  std::uint32_t crc{0};
};
#pragma pack(pop)

static_assert(sizeof(DeliveryBenchMessage) < 128);

inline std::uint32_t Crc32(void const* data, std::size_t size) noexcept {
  auto const* bytes = static_cast<std::uint8_t const*>(data);
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (int b = 0; b < 8; ++b) {
      auto const mask =
          static_cast<std::uint32_t>(-(static_cast<int>(crc & 1u)));
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

inline std::uint32_t DeliveryBenchMessageCrc(
    DeliveryBenchMessage const& msg) noexcept {
  auto copy = msg;
  copy.crc = 0;
  return Crc32(&copy, sizeof(copy));
}

inline void EncodeDeliveryBenchMessage(DeliveryBenchMessage& msg) noexcept {
  msg.magic = kBenchMagic;
  msg.version = kBenchVersion;
  msg.crc = DeliveryBenchMessageCrc(msg);
}

inline bool DecodeDeliveryBenchMessage(std::uint8_t const* data,
                                       std::size_t size,
                                       DeliveryBenchMessage& out) noexcept {
  if (data == nullptr || size < sizeof(DeliveryBenchMessage)) {
    return false;
  }
  std::memcpy(&out, data, sizeof(DeliveryBenchMessage));
  if (out.magic != kBenchMagic || out.version != kBenchVersion) {
    return false;
  }
  return out.crc == DeliveryBenchMessageCrc(out);
}

inline std::vector<std::uint8_t> SerializeDeliveryBenchMessage(
    DeliveryBenchMessage msg) {
  EncodeDeliveryBenchMessage(msg);
  auto const* bytes = reinterpret_cast<std::uint8_t const*>(&msg);
  return {bytes, bytes + sizeof(msg)};
}

inline std::optional<DeliveryBenchMessage> DeserializeDeliveryBenchMessage(
    std::uint8_t const* data, std::size_t size) {
  DeliveryBenchMessage out{};
  if (!DecodeDeliveryBenchMessage(data, size, out)) {
    return std::nullopt;
  }
  return out;
}

}  // namespace ae::bench::uap

#endif  // EXAMPLES_BENCHES_AETHER_UAP_DELIVERY_TIMING_BENCH_COMMON_BENCH_MESSAGE_H_
