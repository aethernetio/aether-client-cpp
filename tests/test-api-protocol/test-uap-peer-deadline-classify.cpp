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

#include <unity.h>

#include <chrono>
#include <optional>

#include "aether/receive_schedule.h"
#include "examples/aether_uap_peer_deadline_test/missed_deadline.h"

namespace ae::test_uap_peer_deadline_classify {
namespace {

TimePoint Tp(std::int64_t ms) {
  return TimePoint{} + std::chrono::milliseconds{ms};
}

PeerReceiveSchedule Make(std::int64_t last_ms,
                         std::optional<std::int64_t> next_ms) {
  PeerReceiveSchedule s{};
  s.last_ping = Tp(last_ms);
  if (next_ms.has_value()) {
    s.next_ping_deadline = Tp(*next_ms);
  }
  return s;
}

}  // namespace

void test_IsMissedDeadline_BeforeDeadlineUnchangedPing() {
  auto const prev = Make(1000, 4000);
  auto const curr = Make(1000, 4000);
  TEST_ASSERT_FALSE(
      ae::test_uap_peer_deadline::IsMissedDeadline(prev, curr, Tp(3500)));
}

void test_IsMissedDeadline_AfterDeadlineUnchangedPing() {
  auto const prev = Make(1000, 4000);
  auto const curr = Make(1000, 4000);
  TEST_ASSERT_TRUE(
      ae::test_uap_peer_deadline::IsMissedDeadline(prev, curr, Tp(4500)));
}

void test_IsMissedDeadline_AfterDeadlineAdvancedPing() {
  auto const prev = Make(1000, 4000);
  auto const curr = Make(4100, 7100);
  TEST_ASSERT_FALSE(
      ae::test_uap_peer_deadline::IsMissedDeadline(prev, curr, Tp(4500)));
}

void test_IsMissedDeadline_AfterDeadlineTinyDriftNotAdvanced() {
  auto const prev = Make(1000, 4000);
  // 20ms drift is below kLastPingAdvanceEpsilon.
  auto const curr = Make(1020, 4000);
  TEST_ASSERT_TRUE(
      ae::test_uap_peer_deadline::IsMissedDeadline(prev, curr, Tp(4500)));
}

void test_IsMissedDeadline_NoNextDeadline() {
  auto const prev = Make(1000, std::nullopt);
  auto const curr = Make(1000, std::nullopt);
  TEST_ASSERT_FALSE(
      ae::test_uap_peer_deadline::IsMissedDeadline(prev, curr, Tp(99999)));
}

}  // namespace ae::test_uap_peer_deadline_classify

int test_uap_peer_deadline_classify() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_uap_peer_deadline_classify::
               test_IsMissedDeadline_BeforeDeadlineUnchangedPing);
  RUN_TEST(ae::test_uap_peer_deadline_classify::
               test_IsMissedDeadline_AfterDeadlineUnchangedPing);
  RUN_TEST(ae::test_uap_peer_deadline_classify::
               test_IsMissedDeadline_AfterDeadlineAdvancedPing);
  RUN_TEST(ae::test_uap_peer_deadline_classify::
               test_IsMissedDeadline_AfterDeadlineTinyDriftNotAdvanced);
  RUN_TEST(ae::test_uap_peer_deadline_classify::
               test_IsMissedDeadline_NoNextDeadline);
  return UNITY_END();
}
