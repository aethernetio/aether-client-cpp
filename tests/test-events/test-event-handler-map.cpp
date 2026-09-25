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

#include "aether/events/details/event_handler.h"
#include "aether/events/details/event_handler_map.h"

namespace ae::test_event_handler_map {
struct TestHandler : public events::IHandler {
  explicit TestHandler(int& destructions) : destructions_{&destructions} {}
  TestHandler(TestHandler&& other) noexcept
      : TestHandler{other.destructions_, other} {}
  ~TestHandler() override { ++*destructions_; }

 private:
  TestHandler(int* destructions, TestHandler& other) noexcept
      : IHandler{std::move(other)}, destructions_{destructions} {}

  int* destructions_;
};

using Map =
    events::EventHandlerMap<3, sizeof(TestHandler), alignof(TestHandler)>;

void test_DeactivateImmediatelyDestroysHandler() {
  int destructions = 0;
  auto map = Map{};
  auto* handler = map.Add(1, TestHandler{destructions});
  TEST_ASSERT_TRUE(handler != nullptr);
  auto const baseline = destructions;

  map.Deactivate(1, handler);

  TEST_ASSERT_EQUAL(baseline + 1, destructions);
  auto const [begin, end] = map.Get(1);
  TEST_ASSERT_TRUE(begin == end);
}

void test_DeactivateDefersReferencedHandler() {
  int destructions = 0;
  auto map = Map{};
  auto* handler = map.Add(1, TestHandler{destructions});
  TEST_ASSERT_TRUE(handler != nullptr);
  auto const baseline = destructions;
  ++handler->ref_cntr;

  map.Deactivate(1, handler);

  TEST_ASSERT_FALSE(handler->active);
  TEST_ASSERT_EQUAL(baseline, destructions);
  --handler->ref_cntr;
  map.Deactivate(1, handler);
  TEST_ASSERT_EQUAL(baseline + 1, destructions);
}

void test_PruneRetainsReferencedHandlersWithoutDeactivating() {
  int first_destructions = 0;
  int second_destructions = 0;
  auto map = Map{};
  auto* first = map.Add(1, TestHandler{first_destructions});
  auto* second = map.Add(1, TestHandler{second_destructions});
  TEST_ASSERT_TRUE(first != nullptr);
  TEST_ASSERT_TRUE(second != nullptr);
  auto const first_baseline = first_destructions;
  auto const second_baseline = second_destructions;
  ++first->ref_cntr;

  map.Prune(1);

  TEST_ASSERT_TRUE(first->active);
  TEST_ASSERT_EQUAL(first_baseline, first_destructions);
  TEST_ASSERT_EQUAL(second_baseline + 1, second_destructions);
  auto const [begin, end] = map.Get(1);
  TEST_ASSERT_TRUE(begin != end);
  TEST_ASSERT_EQUAL(first, begin->second);
  --first->ref_cntr;
  map.Prune(1);
  TEST_ASSERT_EQUAL(first_baseline + 1, first_destructions);
}

void test_PruneDeactivatedRetainsActiveHandlers() {
  int active_destructions = 0;
  int inactive_destructions = 0;
  auto map = Map{};
  auto* active = map.Add(1, TestHandler{active_destructions});
  auto* inactive = map.Add(1, TestHandler{inactive_destructions});
  TEST_ASSERT_TRUE(active != nullptr);
  TEST_ASSERT_TRUE(inactive != nullptr);
  auto const active_baseline = active_destructions;
  auto const inactive_baseline = inactive_destructions;

  inactive->active = false;
  map.PruneDeactivated(1);

  TEST_ASSERT_EQUAL(active_baseline, active_destructions);
  TEST_ASSERT_EQUAL(inactive_baseline + 1, inactive_destructions);
  auto const [begin, end] = map.Get(1);
  TEST_ASSERT_TRUE(begin != end);
  TEST_ASSERT_EQUAL(active, begin->second);
}

void test_MapTeardownDestroysHandlers() {
  int destructions = 0;
  {
    auto map = Map{};
    TEST_ASSERT_TRUE(map.Add(1, TestHandler{destructions}) != nullptr);
    TEST_ASSERT_TRUE(map.Add(2, TestHandler{destructions}) != nullptr);
  }
  // Each temporary and each stored concrete handler is destroyed exactly once.
  TEST_ASSERT_EQUAL(4, destructions);
}

void test_CapacityOverflowAndReuseAfterPrune() {
  int destructions = 0;
  auto map = Map{};
  auto* first = map.Add(1, TestHandler{destructions});
  auto* second = map.Add(2, TestHandler{destructions});
  auto* third = map.Add(3, TestHandler{destructions});
  TEST_ASSERT_TRUE(first != nullptr);
  TEST_ASSERT_TRUE(second != nullptr);
  TEST_ASSERT_TRUE(third != nullptr);
  TEST_ASSERT_TRUE(map.Add(4, TestHandler{destructions}) == nullptr);

  map.Prune(1);
  TEST_ASSERT_TRUE(map.Add(4, TestHandler{destructions}) != nullptr);
}

}  // namespace ae::test_event_handler_map

int test_event_handler_map() {
  using namespace ae::test_event_handler_map;  // NOLINT
  UNITY_BEGIN();
  RUN_TEST(test_DeactivateImmediatelyDestroysHandler);
  RUN_TEST(test_DeactivateDefersReferencedHandler);
  RUN_TEST(test_PruneRetainsReferencedHandlersWithoutDeactivating);
  RUN_TEST(test_PruneDeactivatedRetainsActiveHandlers);
  RUN_TEST(test_MapTeardownDestroysHandlers);
  RUN_TEST(test_CapacityOverflowAndReuseAfterPrune);
  return UNITY_END();
}
