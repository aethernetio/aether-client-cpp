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

#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_SCHEDULE_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_SCHEDULE_H_

#include <cstdint>

#include "stream_matrix/bench_clock.h"

namespace ae::stream_matrix_bench {

struct ScheduleSlotDecision {
  bool should_send{false};
  BenchInstant slot_deadline{};
  std::uint64_t missed_slots{};
  BenchInstant next_deadline{};
};

// Grid slot at `next_deadline`. At most one send per call.
// `window_end` is exclusive (measure_end / warmup boundary).
ScheduleSlotDecision EvaluateScheduleSlot(BenchInstant now,
                                          BenchInstant window_end,
                                          BenchInstant next_deadline,
                                          BenchDuration period);

}  // namespace ae::stream_matrix_bench

#endif
