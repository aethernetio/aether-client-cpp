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

#include <chrono>
#include <cstdlib>
#include <iostream>

#include "stream_matrix/bench_clock.h"
#include "stream_matrix/bench_schedule.h"

using namespace ae::stream_matrix_bench;

static int g_failures = 0;

static void Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

static BenchInstant Ms(std::int64_t ms) {
  return BenchInstant{} + std::chrono::milliseconds{ms};
}

static void test_slightly_late_wakeup() {
  auto end = Ms(10'000);
  auto next = Ms(1000);
  auto period = std::chrono::milliseconds{1000};
  auto d = EvaluateScheduleSlot(Ms(1000) + std::chrono::microseconds{100}, end,
                                next, period);
  Expect(d.should_send, "1000.1ms sends");
  Expect(d.missed_slots == 0, "no missed");
  Expect(d.slot_deadline == Ms(1000), "slot 1000");
  Expect(d.next_deadline == Ms(2000), "next 2000");
}

static void test_exact_deadline() {
  auto end = Ms(10'000);
  auto next = Ms(1000);
  auto period = std::chrono::milliseconds{1000};
  auto d = EvaluateScheduleSlot(Ms(1000), end, next, period);
  Expect(d.should_send && d.missed_slots == 0 && d.next_deadline == Ms(2000),
         "exact deadline");
}

static void test_multi_period_late() {
  auto end = Ms(10'000);
  auto next = Ms(1000);
  auto period = std::chrono::milliseconds{1000};
  auto d = EvaluateScheduleSlot(Ms(3500), end, next, period);
  Expect(d.should_send, "send at 3500");
  Expect(d.missed_slots == 2, "two missed");
  Expect(d.slot_deadline == Ms(3000), "slot 3000");
  Expect(d.next_deadline == Ms(4000), "next 4000");
}

static void test_repeat_tick_no_time_advance() {
  auto end = Ms(10'000);
  auto next = Ms(1000);
  auto period = std::chrono::milliseconds{1000};
  auto d1 = EvaluateScheduleSlot(Ms(3500), end, next, period);
  auto d2 = EvaluateScheduleSlot(Ms(3500), end, d1.next_deadline, period);
  Expect(d1.should_send, "first sends");
  Expect(!d2.should_send, "repeat tick silent");
}

static void test_before_window_and_after_end() {
  auto measure_begin = Ms(8000);
  auto period = std::chrono::milliseconds{1000};
  auto next = Ms(6000);
  auto early = EvaluateScheduleSlot(Ms(5500), measure_begin, next, period);
  Expect(!early.should_send, "before first warmup slot");
  auto after = EvaluateScheduleSlot(Ms(9000), measure_begin, Ms(7000), period);
  Expect(!after.should_send, "after window end");
}

static void test_simulated_60s_1hz() {
  auto begin = Ms(0);
  auto end = begin + std::chrono::seconds{60};
  auto period = std::chrono::milliseconds{1000};
  auto next = begin;
  std::uint64_t sends = 0;
  for (int i = 0; i < 60; ++i) {
    auto now = begin + std::chrono::milliseconds{i * 1000 + 1};
    auto d = EvaluateScheduleSlot(now, end, next, period);
    next = d.next_deadline;
    if (d.should_send) {
      ++sends;
    }
  }
  Expect(sends == 60, "60 sends with +1ms wake skew");
}

static void test_old_impl_would_fail_multi_late() {
  // Old code: while next < now skip all, then requires now == next for send.
  auto end = Ms(10'000);
  auto next = Ms(1000);
  auto period = std::chrono::milliseconds{1000};
  auto now = Ms(1000) + std::chrono::microseconds{100};
  std::uint64_t missed = 0;
  while (next < now && next < end) {
    ++missed;
    next += period;
  }
  bool old_send = (now >= next && now < end);
  Expect(!old_send, "old impl skips 1000.1ms case");
  auto d = EvaluateScheduleSlot(now, end, Ms(1000), period);
  Expect(d.should_send, "new impl sends");
}

int main() {
  test_slightly_late_wakeup();
  test_exact_deadline();
  test_multi_period_late();
  test_repeat_tick_no_time_advance();
  test_before_window_and_after_end();
  test_simulated_60s_1hz();
  test_old_impl_would_fail_multi_late();
  if (g_failures != 0) {
    std::cerr << "stream_matrix_schedule_test: " << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "stream_matrix_schedule_test: OK\n";
  return 0;
}
