/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_API_BENCH_DELAYS_API_H_
#define EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_API_BENCH_DELAYS_API_H_

#include <array>

#include "aether/crc.h"
#include "aether/reflect/reflect.h"
#include "aether/api_protocol/api_protocol.h"

namespace ae::bench {
class BenchDelaysApi : public ApiClass {
 public:
  static constexpr auto kClassId =
      crc32::checksum_from_literal("ae::bench::BenchDelaysApi");

  struct WarmUp : public Message<WarmUp> {
    static constexpr auto kMessageCode = 3;
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ae::bench::BenchDelaysApi::WarmUp");

    WarmUp() = default;
    explicit WarmUp(std::uint16_t i) : id{i} {}

    AE_CLASS_REFLECT(id, payload)

    std::uint16_t id;
    std::array<std::uint8_t, 98> payload;
  };

  struct TwoByte : public Message<TwoByte> {
    static constexpr auto kMessageCode = 4;
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ae::bench::BenchDelaysApi::TwoByte");

    TwoByte() = default;
    explicit TwoByte(std::uint16_t i) : id{i} {}

    AE_CLASS_REFLECT(id)

    std::uint16_t id;
  };

  struct TenBytes : public Message<TenBytes> {
    static constexpr auto kMessageCode = 5;
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ae::bench::BenchDelaysApi::TenBytes");

    TenBytes() = default;
    explicit TenBytes(std::uint16_t i) : id{i} {}

    AE_CLASS_REFLECT(id, payload)

    std::uint16_t id;
    std::array<std::uint8_t, 8> payload;
  };

  struct HundredBytes : public Message<HundredBytes> {
    static constexpr auto kMessageCode = 6;
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ae::bench::BenchDelaysApi::HundredBytes");

    HundredBytes() = default;
    explicit HundredBytes(std::uint16_t i) : id{i} {}

    AE_CLASS_REFLECT(id, payload)

    std::uint16_t id;
    std::array<std::uint8_t, 98> payload;
  };

  struct ThousandBytes : public Message<ThousandBytes> {
    static constexpr auto kMessageCode = 7;
    static constexpr auto kMessageId = crc32::checksum_from_literal(
        "ae::bench::BenchDelaysApi::ThousandBytes");

    ThousandBytes() = default;
    explicit ThousandBytes(std::uint16_t i) : id{i} {}

    AE_CLASS_REFLECT(id, payload)

    std::uint16_t id;
    std::array<std::uint8_t, 998> payload;
  };

  struct ThousandAndHalfBytes : public Message<ThousandAndHalfBytes> {
    static constexpr auto kMessageCode = 8;
    static constexpr auto kMessageId = crc32::checksum_from_literal(
        "ae::bench::BenchDelaysApi::ThousandAndHalfBytes");

    ThousandAndHalfBytes() = default;
    explicit ThousandAndHalfBytes(std::uint16_t i) : id{i} {}

    AE_CLASS_REFLECT(id, payload)

    std::uint16_t id;
    std::array<std::uint8_t, 1398> payload;
  };

  void LoadFactory(MessageId message_code, ApiParser& parser) override;

  template <typename T>
  void Execute(T&& message, ApiParser& parser) {
    parser.Context().MessageNotify(std::forward<T>(message));
  }

  template <typename T>
  void Pack(T&& message, ApiPacker& packer) {
    packer.Pack(T::kMessageCode, std::forward<T>(message));
  }
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_API_BENCH_DELAYS_API_H_
