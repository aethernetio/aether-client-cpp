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

#ifndef AETHER_ADAPTIVE_DATAGRAM_ADAPTIVE_DATAGRAM_PROTOCOL_H_
#define AETHER_ADAPTIVE_DATAGRAM_ADAPTIVE_DATAGRAM_PROTOCOL_H_

#include <array>
#include <cstdint>

#include "aether/api_protocol/api_protocol.h"
#include "aether/types/data_buffer.h"

namespace ae::adaptive_datagram {
inline constexpr std::size_t kReceiptBits = 512;
inline constexpr std::size_t kReceiptWords = kReceiptBits / 64;

struct Datagram {
  std::uint64_t session{};
  std::uint32_t stream{};
  std::uint64_t sequence{};
  std::uint64_t queued_at_us{};
  std::uint64_t sent_at_us{};
  std::uint32_t lifetime_us{};
  DataBuffer payload;
  AE_REFLECT_MEMBERS(session, stream, sequence, queued_at_us, sent_at_us,
                     lifetime_us, payload)
};
// Repeated selective receipts survive feedback loss and reordering. Bit zero
// refers to largest_sequence. Separate masks retain late arrivals as receipts.
struct Feedback {
  std::uint64_t session{};
  std::uint32_t stream{};
  std::uint64_t report{};
  std::uint64_t largest_sequence{};
  std::array<std::uint64_t, kReceiptWords> received{};
  std::array<std::uint64_t, kReceiptWords> on_time{};
  std::uint64_t echo_sent_at_us{};
  std::uint64_t received_at_us{};
  std::uint64_t sampled_sequence{};
  std::uint64_t reported_at_us{};
  AE_REFLECT_MEMBERS(session, stream, report, largest_sequence, received,
                     on_time, echo_sent_at_us, received_at_us, sampled_sequence,
                     reported_at_us)
};

class IAdaptiveDatagramApi : public DeclareApi<IAdaptiveDatagramApi> {
 public:
  virtual ~IAdaptiveDatagramApi() = default;
  virtual void Data(Datagram datagram) = 0;
  virtual void Report(Feedback feedback) = 0;
  API_LIST(METHOD(3, Data), METHOD(4, Report))
};
}  // namespace ae::adaptive_datagram
#endif  // AETHER_ADAPTIVE_DATAGRAM_ADAPTIVE_DATAGRAM_PROTOCOL_H_
