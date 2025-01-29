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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_BANDWIDTH_API_H_
#define EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_BANDWIDTH_API_H_

#include <cstdint>
#include <array>

#include "aether/crc.h"
#include "aether/api_protocol/api_protocol.h"

namespace ae::bench {
class BandwidthApi : public ApiClass, ExtendsApi<ReturnResultApi> {
 public:
  static constexpr auto kClassId =
      crc32::checksum_from_literal("ae::bench::BandwidthApi");

  // sender sends handshake until receiver doesn't answers true
  struct Handshake : public Message<Handshake> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ae::bench::BandwidthApi::Handshake");
    static constexpr auto kMessageCode = 3;

    template <typename T>
    void Serializator(T& s) {
      s & request_id;
    }

    RequestId request_id;
  };

  // sender sends sync and wait for receiver's response true
  struct Sync : public Message<Sync> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ae::bench::BandwidthApi::Sync");
    static constexpr auto kMessageCode = 4;

    template <typename T>
    void Serializator(T& s) {
      s & request_id;
    }

    RequestId request_id;
  };

  // warm up server's cache
  struct WarmUp : public Message<WarmUp> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ae::bench::BandwidthApi::WarmUp");
    static constexpr auto kMessageCode = 5;

    template <typename T>
    void Serializator(T& s) {
      s & payload;
    }

    std::array<std::uint8_t, 100> payload;
  };

  struct OneByte : public Message<OneByte> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ae::bench::BandwidthApi::OneByte");
    static constexpr auto kMessageCode = 6;

    template <typename T>
    void Serializator(T& s) {
      s & payload;
    }

    std::uint8_t payload;
  };

  struct TenBytes : public Message<TenBytes> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ae::bench::BandwidthApi::TenBytes");
    static constexpr auto kMessageCode = 7;

    template <typename T>
    void Serializator(T& s) {
      s & payload;
    }
    std::array<std::uint8_t, 10> payload;
  };

  struct HundredBytes : public Message<HundredBytes> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ae::bench::BandwidthApi::HundredBytes");
    static constexpr auto kMessageCode = 8;

    template <typename T>
    void Serializator(T& s) {
      s & payload;
    }
    std::array<std::uint8_t, 100> payload;
  };

  struct ThousandBytes : public Message<ThousandBytes> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ae::bench::BandwidthApi::ThousandBytes");
    static constexpr auto kMessageCode = 9;

    template <typename T>
    void Serializator(T& s) {
      s & payload;
    }
    std::array<std::uint8_t, 1000> payload;
  };

  struct VarMessageSize : public Message<VarMessageSize> {
    static constexpr auto kMessageId = crc32::checksum_from_literal(
        "ae::bench::BandwidthApi::ThousandAndHalfBytes");
    static constexpr auto kMessageCode = 10;

    VarMessageSize() = default;
    explicit VarMessageSize(std::size_t size) : payload(size) {}

    template <typename T>
    void Serializator(T& s) {
      s & payload;
    }

    std::vector<std::uint8_t> payload;
  };

  void LoadFactory(MessageId message_code, ApiParser& api_parser) override;

  template <typename T>
  void Execute(T&& message, ApiParser& api_parser);

  template <typename T>
  void Pack(T&& message, ApiPacker& api_packer) {
    api_packer.Pack(T::kMessageCode, std::forward<T>(message));
  }
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_BANDWIDTH_API_H_
