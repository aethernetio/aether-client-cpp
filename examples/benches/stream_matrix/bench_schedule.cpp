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

#include "stream_matrix/bench_schedule.h"

namespace ae::stream_matrix_bench {

ScheduleSlotDecision EvaluateScheduleSlot(BenchInstant now,
                                          BenchInstant window_end,
                                          BenchInstant next_deadline,
                                          BenchDuration period) {
  ScheduleSlotDecision out;
  out.next_deadline = next_deadline;

  if (period.count() <= 0) {
    return out;
  }
  if (now >= window_end) {
    return out;
  }
  if (now < next_deadline) {
    return out;
  }
  if (next_deadline >= window_end) {
    return out;
  }

  BenchInstant slot = next_deadline;
  std::uint64_t missed = 0;

  while (slot + period <= now && slot + period < window_end) {
    ++missed;
    slot += period;
  }

  if (now >= slot && slot < window_end) {
    out.should_send = true;
    out.slot_deadline = slot;
    out.missed_slots = missed;
    out.next_deadline = slot + period;
  } else {
    out.next_deadline = slot;
  }

  return out;
}

}  // namespace ae::stream_matrix_bench
