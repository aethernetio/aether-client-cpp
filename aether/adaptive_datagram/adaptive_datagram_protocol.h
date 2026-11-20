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

#include <cstdint>

#include "aether/adaptive_datagram/adaptive_datagram_time.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/types/data_buffer.h"

namespace ae::adaptive_datagram {

struct Datagram : DatagramTiming {
  std::uint64_t session{};
  std::uint32_t stream{};
  std::uint64_t sequence{};
  DataBuffer payload;
  std::uint16_t receipt_window{};
  AE_REFLECT(AE_MMBRS(session, stream, sequence, receipt_window),
             AE_REF_BASE(DatagramTiming), AE_MMBR(payload))
};
// A page covers count consecutive packets, starting at largest_sequence minus
// range_offset and descending. Each page is independently decodable.
struct Feedback : FeedbackTiming {
  std::uint64_t session{};
  std::uint32_t stream{};
  std::uint64_t report{};
  std::uint64_t largest_sequence{};
  std::uint16_t range_offset{};
  std::uint16_t count{};
  std::uint8_t encoding{};
  std::uint64_t sampled_sequence{};
  DataBuffer statuses;
  AE_REFLECT(AE_MMBRS(session, stream, report, largest_sequence, range_offset,
                      count, encoding, sampled_sequence),
             AE_REF_BASE(FeedbackTiming), AE_MMBR(statuses))
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
