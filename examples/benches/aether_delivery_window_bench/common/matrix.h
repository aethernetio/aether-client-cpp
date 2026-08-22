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

#ifndef AETHER_DELIVERY_WINDOW_BENCH_COMMON_MATRIX_H_
#define AETHER_DELIVERY_WINDOW_BENCH_COMMON_MATRIX_H_

#include <vector>

#include "bench_buckets.h"
#include "bench_types.h"

namespace ae::bench::dw {

inline std::vector<Bucket> BucketsForTiming(TimingConfig const& t) {
  std::vector<Bucket> out;
  out.push_back(Bucket::kInsideEarly);
  if (OffsetForBucket(Bucket::kInsideLate, t.actual_ping_interval_ms,
                      t.rx_window_ms)) {
    out.push_back(Bucket::kInsideLate);
  }
  if (OutsideBucketsAllowed(t.actual_ping_interval_ms, t.rx_window_ms)) {
    out.push_back(Bucket::kOutsideEarly);
    out.push_back(Bucket::kOutsideLate);
  }
  out.push_back(Bucket::kBeforeNextPing);
  return out;
}

inline std::vector<MatrixConfig> QuickMatrix() {
  std::vector<MatrixConfig> configs;

  {
    MatrixConfig c;
    c.id = "Q1";
    c.timing = {6000, 6000, 6000};
    c.buckets = {Bucket::kInsideEarly, Bucket::kInsideLate,
                 Bucket::kBeforeNextPing};
    c.samples_per_bucket = 5;
    configs.push_back(c);
  }
  {
    MatrixConfig c;
    c.id = "Q2";
    c.timing = {4000, 4000, 1000};
    c.buckets = {Bucket::kInsideEarly, Bucket::kInsideLate,
                 Bucket::kOutsideEarly, Bucket::kOutsideLate,
                 Bucket::kBeforeNextPing};
    c.samples_per_bucket = 10;
    configs.push_back(c);
  }
  {
    MatrixConfig c;
    c.id = "Q3";
    c.timing = {2000, 2000, 250};
    c.buckets = {Bucket::kInsideEarly, Bucket::kOutsideEarly,
                 Bucket::kOutsideLate, Bucket::kBeforeNextPing};
    c.samples_per_bucket = 10;
    configs.push_back(c);
  }
  {
    MatrixConfig c;
    c.id = "Q4";
    c.timing = {4000, 8000, 1000};
    c.buckets = BucketsForTiming(c.timing);
    c.samples_per_bucket = 5;
    configs.push_back(c);
  }
  {
    MatrixConfig c;
    c.id = "Q5";
    c.timing = {4000, 0, 1000};
    c.buckets = BucketsForTiming(c.timing);
    c.samples_per_bucket = 5;
    configs.push_back(c);
  }
  return configs;
}

// Minimal live-cloud harness check (not the full QUICK matrix).
inline std::vector<MatrixConfig> SmokeMatrix() {
  MatrixConfig c;
  c.id = "Q2";
  c.timing = {4000, 4000, 1000};
  c.buckets = {Bucket::kInsideEarly, Bucket::kOutsideEarly};
  c.samples_per_bucket = 1;
  return {c};
}

inline std::uint32_t ConfigIdHash(std::string const& id) {
  std::uint32_t h = 2166136261u;
  for (char c : id) {
    h ^= static_cast<std::uint8_t>(c);
    h *= 16777619u;
  }
  return h;
}

}  // namespace ae::bench::dw

#endif  // AETHER_DELIVERY_WINDOW_BENCH_COMMON_MATRIX_H_
