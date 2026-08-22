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

#ifndef AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_STATS_H_
#define AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_STATS_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace ae::bench::dw {

inline std::optional<double> Quantile(std::vector<double> values,
                                      double q) noexcept {
  if (values.empty()) {
    return std::nullopt;
  }
  if (q < 0.0) {
    q = 0.0;
  }
  if (q > 1.0) {
    q = 1.0;
  }
  std::sort(values.begin(), values.end());
  if (values.size() == 1) {
    return values.front();
  }
  auto const pos = q * static_cast<double>(values.size() - 1);
  auto const lo = static_cast<std::size_t>(std::floor(pos));
  auto const hi = static_cast<std::size_t>(std::ceil(pos));
  if (lo == hi) {
    return values[lo];
  }
  auto const w = pos - static_cast<double>(lo);
  return values[lo] * (1.0 - w) + values[hi] * w;
}

struct QuantileSummary {
  std::optional<double> p50;
  std::optional<double> p95;
  std::optional<double> max;
};

inline QuantileSummary Summarize(std::vector<double> const& values) noexcept {
  QuantileSummary s;
  if (values.empty()) {
    return s;
  }
  s.p50 = Quantile(values, 0.50);
  s.p95 = Quantile(values, 0.95);
  s.max = *std::max_element(values.begin(), values.end());
  return s;
}

}  // namespace ae::bench::dw

#endif  // AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_STATS_H_
