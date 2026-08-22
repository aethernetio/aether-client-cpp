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

#include "bench_message.h"

#include <cstring>

namespace ae::bench::dw {
namespace {

constexpr std::uint32_t kCrcPoly = 0xEDB88320u;

}  // namespace

std::uint32_t Crc32(void const* data, std::size_t size) noexcept {
  auto const* bytes = static_cast<std::uint8_t const*>(data);
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (int b = 0; b < 8; ++b) {
      auto const mask = static_cast<std::uint32_t>(-(crc & 1u));
      crc = (crc >> 1) ^ (kCrcPoly & mask);
    }
  }
  return ~crc;
}

std::uint32_t BenchMessageCrc(BenchMessage const& msg) noexcept {
  BenchMessage tmp = msg;
  tmp.crc = 0;
  return Crc32(&tmp, sizeof(tmp));
}

void EncodeBenchMessage(BenchMessage& msg) noexcept {
  msg.magic = kBenchMagic;
  msg.version = kBenchVersion;
  msg.crc = BenchMessageCrc(msg);
}

bool DecodeBenchMessage(std::uint8_t const* data, std::size_t size,
                        BenchMessage& out) noexcept {
  if (data == nullptr || size < sizeof(BenchMessage)) {
    return false;
  }
  std::memcpy(&out, data, sizeof(BenchMessage));
  if (out.magic != kBenchMagic || out.version != kBenchVersion) {
    return false;
  }
  auto const expect = BenchMessageCrc(out);
  return expect == out.crc;
}

std::vector<std::uint8_t> SerializeBenchMessage(BenchMessage msg) {
  EncodeBenchMessage(msg);
  std::vector<std::uint8_t> out(sizeof(BenchMessage));
  std::memcpy(out.data(), &msg, sizeof(BenchMessage));
  return out;
}

std::optional<BenchMessage> DeserializeBenchMessage(std::uint8_t const* data,
                                                    std::size_t size) {
  BenchMessage msg{};
  if (!DecodeBenchMessage(data, size, msg)) {
    return std::nullopt;
  }
  return msg;
}

}  // namespace ae::bench::dw
