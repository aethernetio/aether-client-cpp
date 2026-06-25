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

#ifndef AETHER_STREAM_API_API_CALL_ADAPTER_H_
#define AETHER_STREAM_API_API_CALL_ADAPTER_H_

#include <utility>
#include <limits>
#include <iostream>
#include <cstdint>
#include <cstddef>
#include <cassert>

#include "aether/stream_api/istream.h"
#include "aether/api_protocol/api_context.h"
#include "aether/prepared_packet/packet_encoder.h"
#include "aether/types/data_buffer.h"

namespace ae {

// FASTTX_STEP3_ENCODE_PACKET_API
namespace fast_tx_internal {

enum class EncodePacketError {
  kNone = 0,
  kNonceExhausted,
  kOutputBufferTooSmall,
  kPayloadTooLarge,
  kEncodeFailed,
};

inline char const* ToString(EncodePacketError error) {
  switch (error) {
    case EncodePacketError::kNone:
      return "none";
    case EncodePacketError::kNonceExhausted:
      return "nonce_exhausted";
    case EncodePacketError::kOutputBufferTooSmall:
      return "output_buffer_too_small";
    case EncodePacketError::kPayloadTooLarge:
      return "payload_too_large";
    case EncodePacketError::kEncodeFailed:
      return "encode_failed";
  }
  return "unknown";
}

struct EncodePacketReport {
  EncodePacketError error = EncodePacketError::kNone;
  std::size_t bytes_written = 0;
  std::uint64_t nonce_used = 0;

  explicit operator bool() const { return error == EncodePacketError::kNone; }
};

// Endpoint intentionally stays outside.
// External code will own ip:port and raw UDP socket.
// This struct must contain only data needed by Aether packet encoding.
struct PreparedPacketEncoder {
  std::uint64_t next_nonce = 0;
  std::uint64_t nonce_limit = (std::numeric_limits<std::uint64_t>::max)();
  std::size_t max_packet_size = 1200;

  EncodePacketReport TakeNonce() {
    if (next_nonce == nonce_limit) {
      return EncodePacketReport{EncodePacketError::kNonceExhausted, 0, 0};
    }

    auto nonce = next_nonce;
    ++next_nonce;

    return EncodePacketReport{EncodePacketError::kNone, 0, nonce};
  }
};

// Future external shape:
//
//   EncodePacketResult EncodePacket(PreparedPacketEncoder& prepared,
//                                   Span<const std::uint8_t> payload,
//                                   DataBuffer& out);
//
// Current internal step keeps old Aether path alive by using ByteIStream as out.
struct EncodePacketResult {
  EncodePacketReport report;
  WriteAction* action = nullptr;

  explicit operator bool() const {
    return static_cast<bool>(report) && (action != nullptr);
  }
};

template <typename TApi>
EncodePacketResult EncodePacket(PreparedPacketEncoder& prepared,
                                ApiContext<TApi>&& data,
                                ByteIStream& out) {
  auto report = prepared.TakeNonce();
  if (!report) {
    return EncodePacketResult{report, nullptr};
  }

  auto& action = out.Write(std::move(data));

  return EncodePacketResult{report, &action};
}

}  // namespace fast_tx_internal

/**
 * \brief Api method call adapter to automatically flush the packet after all
 * method calls.
 */
template <typename TApi>
class ApiCallAdapter {
 public:
  ApiCallAdapter(ApiContext<TApi>&& api_context, ByteIStream& byte_stream)
      : api_context_{std::move(api_context)}, byte_stream_{&byte_stream} {}

  AE_CLASS_MOVE_ONLY(ApiCallAdapter)

  WriteAction& Flush() {
    fast_tx_internal::PreparedPacketEncoder prepared{};

    auto result = fast_tx_internal::EncodePacket(
        prepared, std::move(api_context_), *byte_stream_);

    if (!result) {
      std::cerr << "FastTx EncodePacket failed: "
                << fast_tx_internal::ToString(result.report.error)
                << "\n";
      assert(false);
    }

    return *result.action;
  }

  ApiContext<TApi>& operator->() { return api_context_; }

 private:
  ApiContext<TApi> api_context_;
  ByteIStream* byte_stream_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_API_CALL_ADAPTER_H_
