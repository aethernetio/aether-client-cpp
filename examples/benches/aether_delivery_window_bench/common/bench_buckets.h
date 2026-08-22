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

#ifndef AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_BUCKETS_H_
#define AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_BUCKETS_H_

#include <algorithm>
#include <cstdint>
#include <optional>

#include "bench_types.h"

namespace ae::bench::dw {

inline bool OutsideBucketsAllowed(std::int64_t actual_ms,
                                  std::int64_t rx_window_ms) noexcept {
  return (actual_ms - rx_window_ms) >= 750;
}

inline std::optional<std::int64_t> OffsetForBucket(
    Bucket bucket, std::int64_t actual_ms, std::int64_t rx_window_ms) noexcept {
  auto const I = actual_ms;
  auto const W = rx_window_ms;
  switch (bucket) {
    case Bucket::kInsideEarly:
      return (std::min)(static_cast<std::int64_t>(250), W / 4);
    case Bucket::kInsideLate:
      if (W < 400) {
        return std::nullopt;
      }
      return (W * 3) / 4;
    case Bucket::kOutsideEarly:
      if (!OutsideBucketsAllowed(I, W)) {
        return std::nullopt;
      }
      return W + (std::max)(static_cast<std::int64_t>(250), (I - W) / 4);
    case Bucket::kOutsideLate:
      if (!OutsideBucketsAllowed(I, W)) {
        return std::nullopt;
      }
      return W + ((I - W) * 3) / 4;
    case Bucket::kBeforeNextPing:
      return I - 250;
    default:
      return std::nullopt;
  }
}

// Classify where server_accept falls relative to [window_open, window_close).
inline Bucket ClassifyAcceptBucket(std::int64_t accept_us,
                                   std::int64_t window_open_us,
                                   std::int64_t window_close_us,
                                   std::int64_t actual_ms,
                                   std::int64_t rx_window_ms,
                                   Bucket target) noexcept {
  if (window_open_us <= 0 || accept_us <= 0) {
    return Bucket::kUnknown;
  }
  auto const offset_ms = (accept_us - window_open_us) / 1000;
  auto const I = actual_ms;
  auto const W = rx_window_ms;

  auto near = [](std::int64_t value, std::int64_t expect,
                 std::int64_t tol) noexcept {
    auto const d = value > expect ? value - expect : expect - value;
    return d <= tol;
  };

  // Prefer matching the target bucket with a small tolerance so boundary
  // jitter can still validate as INVALID_BOUNDARY when far from target.
  if (auto expect = OffsetForBucket(target, I, W)) {
    if (near(offset_ms, *expect, 50)) {
      return target;
    }
  }

  if (accept_us >= window_open_us &&
      (window_close_us <= 0 || accept_us < window_close_us)) {
    if (auto early = OffsetForBucket(Bucket::kInsideEarly, I, W);
        early && near(offset_ms, *early, 80)) {
      return Bucket::kInsideEarly;
    }
    if (auto late = OffsetForBucket(Bucket::kInsideLate, I, W);
        late && near(offset_ms, *late, 80)) {
      return Bucket::kInsideLate;
    }
    // Inside window but not near a named inside bucket.
    return Bucket::kUnknown;
  }

  if (auto before = OffsetForBucket(Bucket::kBeforeNextPing, I, W);
      before && near(offset_ms, *before, 80)) {
    return Bucket::kBeforeNextPing;
  }
  if (auto oe = OffsetForBucket(Bucket::kOutsideEarly, I, W);
      oe && near(offset_ms, *oe, 80)) {
    return Bucket::kOutsideEarly;
  }
  if (auto ol = OffsetForBucket(Bucket::kOutsideLate, I, W);
      ol && near(offset_ms, *ol, 80)) {
    return Bucket::kOutsideLate;
  }
  if (offset_ms >= W) {
    return Bucket::kOutsideEarly;
  }
  return Bucket::kUnknown;
}

inline bool AcceptMatchesTargetBucket(Bucket actual, Bucket target) noexcept {
  return actual == target;
}

}  // namespace ae::bench::dw

#endif  // AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_BUCKETS_H_
