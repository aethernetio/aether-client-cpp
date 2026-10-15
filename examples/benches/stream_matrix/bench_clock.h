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

#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_CLOCK_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_CLOCK_H_

#include <chrono>

namespace ae::stream_matrix_bench {

using BenchClock = std::chrono::steady_clock;
using BenchInstant = BenchClock::time_point;
using BenchDuration = BenchClock::duration;

inline BenchInstant BenchNow() { return BenchClock::now(); }

inline BenchDuration MsToBenchDuration(double period_ms) {
  return std::chrono::duration_cast<BenchDuration>(
      std::chrono::duration<double, std::milli>(period_ms));
}

inline double BenchDurationMs(BenchDuration d) {
  return std::chrono::duration<double, std::milli>(d).count();
}

inline double BenchInstantDeltaMs(BenchInstant from, BenchInstant to) {
  return BenchDurationMs(to - from);
}

}  // namespace ae::stream_matrix_bench

#endif
