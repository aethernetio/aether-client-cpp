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

#ifndef AETHER_DELIVERY_WINDOW_BENCH_COMMON_RING_TRACE_H_
#define AETHER_DELIVERY_WINDOW_BENCH_COMMON_RING_TRACE_H_

#include <array>
#include <cstdint>
#include <fstream>
#include <string>

#include "bench_types.h"

namespace ae::bench::dw {

struct TraceEntry {
  std::int64_t local_steady_us{0};
  std::uint32_t cycle_id{0};
  std::uint32_t message_id{0};
  std::uint16_t server_id{0};
  std::uint8_t event_kind{0};
  std::uint8_t direction{0};
  std::int64_t a{0};
  std::int64_t b{0};
  std::int64_t c{0};
};

template <std::size_t Capacity = 4096>
class RingTrace {
 public:
  void Push(TraceEntry e) noexcept {
    buffer_[write_ % Capacity] = e;
    ++write_;
    if (count_ < Capacity) {
      ++count_;
    } else {
      ++dropped_;
    }
  }

  std::size_t size() const noexcept { return count_; }
  std::uint64_t dropped() const noexcept { return dropped_; }

  bool FlushCsv(std::string const& path) const {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
      return false;
    }
    out << "local_steady_us,cycle_id,message_id,server_id,event_kind,"
           "direction,a,b,c\n";
    auto const start =
        count_ < Capacity ? 0 : (write_ - Capacity);
    for (std::uint64_t i = 0; i < count_; ++i) {
      auto const& e = buffer_[(start + i) % Capacity];
      out << e.local_steady_us << ',' << e.cycle_id << ',' << e.message_id
          << ',' << e.server_id << ',' << static_cast<unsigned>(e.event_kind)
          << ',' << static_cast<unsigned>(e.direction) << ',' << e.a << ','
          << e.b << ',' << e.c << '\n';
    }
    return true;
  }

 private:
  std::array<TraceEntry, Capacity> buffer_{};
  std::uint64_t write_{0};
  std::size_t count_{0};
  std::uint64_t dropped_{0};
};

}  // namespace ae::bench::dw

#endif  // AETHER_DELIVERY_WINDOW_BENCH_COMMON_RING_TRACE_H_
