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

#ifndef AETHER_ADAPTIVE_DATAGRAM_ADAPTIVE_DATAGRAM_CONFIG_H_
#define AETHER_ADAPTIVE_DATAGRAM_ADAPTIVE_DATAGRAM_CONFIG_H_

#include <cstddef>
#include <cstdint>
#include <limits>

#include "aether/clock.h"

namespace ae::adaptive_datagram {

// Runtime policy. Loss and latency targets are adaptation goals, not guarantees
// for an arbitrary network. Durations use the project's microsecond clock unit.
struct Config {
  std::uint32_t max_loss_per_mille{50};
  Duration loss_window{1s};
  Duration latency_target{100ms};
  Duration feedback_interval{10ms};
  Duration feedback_timeout{500ms};
  Duration control_interval{100ms};
  std::uint64_t initial_rate_bytes_per_second{64000};
  std::uint64_t min_rate_bytes_per_second{2000};
  std::uint64_t max_rate_bytes_per_second{4000000};
  std::size_t max_datagram_size{1200};
  std::size_t max_queue_packets{128};
  std::size_t max_in_flight_packets{512};
  std::size_t max_history_packets{4096};
  std::uint32_t min_samples{20};
  std::uint64_t session_id{1};
  // Zero learns the first peer session; a known ID pins the expected peer and
  // rejects delayed datagrams belonging to an older session on this path.
  std::uint64_t peer_session_id{0};
  std::uint32_t stream_id{1};

  // Add this to a peer timestamp to express it on the local clock. The caller
  // must provide a known clock offset (and refresh it after clock changes).
  // An RTT/2 assumption is not clock synchronization and cannot prove that a
  // one-way deadline was met.
  std::int64_t peer_clock_offset_us{0};

  // Explicit bounds keep resource requests and bytes * microseconds arithmetic
  // representable on supported targets. Invalid settings are never clamped.
  [[nodiscard]] bool Validate() const {
    constexpr std::size_t kMaxPacketCount = 1U << 20U;
    constexpr auto kMaxRate =
        std::numeric_limits<std::uint64_t>::max() / 1000000U;
    return max_loss_per_mille <= 1000 && loss_window > Duration::zero() &&
           latency_target > Duration::zero() &&
           feedback_interval > Duration::zero() &&
           feedback_timeout > feedback_interval &&
           control_interval > Duration::zero() &&
           control_interval <= feedback_timeout &&
           min_rate_bytes_per_second > 0 &&
           min_rate_bytes_per_second <= initial_rate_bytes_per_second &&
           initial_rate_bytes_per_second <= max_rate_bytes_per_second &&
           max_rate_bytes_per_second <= kMaxRate && max_datagram_size >= 256 &&
           max_datagram_size <= std::numeric_limits<std::uint16_t>::max() &&
           max_queue_packets > 0 && max_queue_packets <= kMaxPacketCount &&
           max_in_flight_packets > 0 && max_in_flight_packets <= 512 &&
           max_history_packets > 0 && max_history_packets <= kMaxPacketCount &&
           min_samples > 0 && min_samples <= max_history_packets &&
           session_id != 0 && stream_id != 0;
  }
};

}  // namespace ae::adaptive_datagram

#endif  // AETHER_ADAPTIVE_DATAGRAM_ADAPTIVE_DATAGRAM_CONFIG_H_
