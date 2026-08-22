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

#ifndef AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_CLASSIFY_H_
#define AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_CLASSIFY_H_

#include <algorithm>
#include <cstdint>
#include <optional>

#include "bench_types.h"

namespace ae::bench::dw {

struct ClassifyInput {
  std::int64_t window_open_us{0};
  std::int64_t window_close_us{0};
  std::int64_t server_accept_us{0};
  std::int64_t receive_us{0};
  std::int64_t next_window_open_us{0};
  std::int64_t pull_request_us{0};
  std::int64_t actual_interval_ms{0};
  int duplicate_count{0};
  bool pull_control{false};
};

inline bool AcceptInsideWindow(std::int64_t accept_us, std::int64_t open_us,
                               std::int64_t close_us) noexcept {
  if (accept_us <= 0 || open_us <= 0) {
    return false;
  }
  if (accept_us < open_us) {
    return false;
  }
  if (close_us > 0 && accept_us >= close_us) {
    return false;
  }
  return true;
}

inline Classification ClassifySample(ClassifyInput const& in) noexcept {
  if (in.duplicate_count > 0) {
    return Classification::kDuplicate;
  }

  auto const lost_deadline = [&]() -> std::int64_t {
    auto const next_plus =
        in.next_window_open_us > 0 ? in.next_window_open_us + 2'000'000 : 0;
    auto const by_interval =
        in.server_accept_us +
        (2 * in.actual_interval_ms + 2000) * 1000;
    return (std::max)(next_plus, by_interval);
  }();

  if (in.receive_us <= 0) {
    // Caller may still be waiting; treat as lost only when deadline known.
    if (lost_deadline > 0 && in.server_accept_us > 0) {
      // Without a wall clock "now", lost is decided by the coordinator.
      return Classification::kLost;
    }
    return Classification::kLost;
  }

  if (in.pull_control && in.pull_request_us > 0 &&
      in.receive_us >= in.pull_request_us) {
    if (in.next_window_open_us <= 0 ||
        in.receive_us < in.next_window_open_us) {
      return Classification::kPulledExplicitly;
    }
  }

  auto const inside_accept =
      AcceptInsideWindow(in.server_accept_us, in.window_open_us,
                         in.window_close_us);

  if (inside_accept) {
    auto const close_plus =
        in.window_close_us > 0 ? in.window_close_us + 250'000 : 0;
    if (close_plus > 0 && in.receive_us <= close_plus) {
      return Classification::kPushInsideWindow;
    }
    if (in.next_window_open_us > 0 &&
        in.receive_us >= in.next_window_open_us - 100'000) {
      return Classification::kDeferredDespiteActiveWindow;
    }
    return Classification::kDeferredDespiteActiveWindow;
  }

  // Outside window accept.
  if (in.next_window_open_us > 0) {
    if (in.receive_us < in.next_window_open_us - 100'000) {
      return Classification::kPushOutsideWindow;
    }
    if (in.receive_us <= in.next_window_open_us + 1'000'000) {
      return Classification::kDeliveredAtNextWindow;
    }
    return Classification::kLateAfterNextWindow;
  }

  return Classification::kLost;
}

inline bool WindowsMatchServer(std::uint16_t sender_server_id,
                               std::uint16_t receiver_server_id) noexcept {
  return sender_server_id != 0 && sender_server_id == receiver_server_id;
}

}  // namespace ae::bench::dw

#endif  // AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_CLASSIFY_H_
