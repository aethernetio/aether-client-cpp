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

#ifndef AETHER_ADAPTIVE_DATAGRAM_RECEIPT_PAGE_H_
#define AETHER_ADAPTIVE_DATAGRAM_RECEIPT_PAGE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "ae-numeric/tiered_int.h"
#include "aether/adaptive_datagram/adaptive_datagram_protocol.h"

namespace ae::adaptive_datagram::receipt_page_internal {

// Low two bits encode absent (0), late (1), or on time (2). The remaining
// bits encode length minus one. Reuse the stream offset and time code tiers:
// runs through 54 packets fit one byte and a uint16_t window fits four.
using ReceiptRun = TieredInt<std::uint8_t, 215, 10359>;
static_assert(ReceiptRun::kUpper >= (65535U - 1) * 4 + 2);

inline void Encode(Feedback& page, DataBuffer const& ring,
                   std::size_t available, std::size_t budget, bool compact) {
  auto const end = page.largest_sequence - page.range_offset;
  auto const status = [&](std::size_t offset) {
    return ring[(end - offset) % ring.size()];
  };
  auto const dense_count = std::min(available, budget * 4);
  auto const dense_bytes = (dense_count + 3) / 4;
  if (compact) {
    std::size_t covered = 0;
    while (covered < available) {
      auto const state = status(covered);
      auto next = covered + 1;
      while (next < available && status(next) == state) {
        ++next;
      }
      std::uint8_t bytes[ReceiptRun::kMaxWireBytes];
      auto const size =
          ReceiptRun{(next - covered - 1) * 4 + state}.Serialize(bytes);
      if (page.statuses.size() + size > budget) {
        break;
      }
      page.statuses.insert(page.statuses.end(), bytes, bytes + size);
      covered = next;
    }
    if (covered > dense_count ||
        (covered == dense_count && page.statuses.size() < dense_bytes)) {
      page.encoding = 1;
      page.count = static_cast<std::uint16_t>(covered);
      return;
    }
  }
  page.encoding = 0;
  page.count = static_cast<std::uint16_t>(dense_count);
  page.statuses.assign(dense_bytes, 0);
  for (std::size_t i = 0; i < dense_count; ++i) {
    page.statuses[i / 4] |=
        static_cast<std::uint8_t>(status(i) << (2 * (i % 4)));
  }
}

inline bool Decode(Feedback const& page, std::size_t window,
                   DataBuffer& states) {
  if (page.count == 0 || std::size_t{page.count} + page.range_offset > window ||
      std::uint64_t{page.count} + page.range_offset > page.largest_sequence) {
    return false;
  }
  if (page.encoding == 0) {
    if (page.statuses.size() != (std::size_t{page.count} + 3) / 4) {
      return false;
    }
    states.resize(page.count);
    for (std::size_t i = 0; i < states.size(); ++i) {
      states[i] = (page.statuses[i / 4] >> (2 * (i % 4))) & 3;
      if (states[i] == 3) {
        return false;
      }
    }
    auto const remainder = page.count % 4;
    return remainder == 0 || (page.statuses.back() >> (2 * remainder)) == 0;
  }
  if (page.encoding != 1) {
    return false;
  }
  states.reserve(page.count);
  for (std::size_t i = 0; i < page.statuses.size();) {
    auto const* bytes = page.statuses.data() + i;
    auto const size =
        ReceiptRun::WireBytesNeeded(bytes, page.statuses.size() - i);
    if (size == 0) {
      return false;
    }
    ReceiptRun run;
    run.Deserialize(bytes, size);
    auto const state = static_cast<std::uint8_t>(run.value_ % 4);
    auto const length = run.value_ / 4 + 1;
    if (state == 3 || length > page.count - states.size()) {
      return false;
    }
    states.insert(states.end(), length, state);
    i += size;
  }
  return states.size() == page.count;
}

}  // namespace ae::adaptive_datagram::receipt_page_internal
#endif  // AETHER_ADAPTIVE_DATAGRAM_RECEIPT_PAGE_H_
