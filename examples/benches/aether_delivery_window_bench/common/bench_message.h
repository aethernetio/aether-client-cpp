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

#ifndef AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_MESSAGE_H_
#define AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_MESSAGE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "bench_types.h"

namespace ae::bench::dw {

#pragma pack(push, 1)
struct BenchMessage {
  std::uint32_t magic{kBenchMagic};
  std::uint8_t version{kBenchVersion};
  std::uint8_t direction{0};
  std::uint16_t reserved{0};
  std::uint32_t run_id_hash{0};
  std::uint32_t config_id{0};
  std::uint32_t cycle_id{0};
  std::uint32_t message_id{0};
  std::uint32_t crc{0};
};
#pragma pack(pop)

static_assert(sizeof(BenchMessage) < 128);

std::uint32_t Crc32(void const* data, std::size_t size) noexcept;
std::uint32_t BenchMessageCrc(BenchMessage const& msg) noexcept;
void EncodeBenchMessage(BenchMessage& msg) noexcept;
bool DecodeBenchMessage(std::uint8_t const* data, std::size_t size,
                        BenchMessage& out) noexcept;
std::vector<std::uint8_t> SerializeBenchMessage(BenchMessage msg);
std::optional<BenchMessage> DeserializeBenchMessage(
    std::uint8_t const* data, std::size_t size);

}  // namespace ae::bench::dw

#endif  // AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_MESSAGE_H_
