/*
 * Copyright 2024 Aethernet Inc.
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

#include <unity.h>

void setUp() {}
void tearDown() {}

#if defined AE_TEST_EVENTS_BENCH
const bool run_bench_tests = true;
#else
const bool run_bench_tests = false;
#endif

extern int test_events();
extern int test_event_delegates();
extern int test_std_function();
extern int test_events_mt();

int main() {
  auto res = 0;
  res += test_events();
  res += test_events_mt();

  if (run_bench_tests) {
    res += test_event_delegates();
    res += test_std_function();
  }
  return res;
}
