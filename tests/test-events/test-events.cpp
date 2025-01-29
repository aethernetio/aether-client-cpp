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

#include "aether/events/events.h"
#include "aether/events/multi_subscription.h"

namespace ae::test_events {

void test_EventCreate() {
  Event<void(int)> event;
  bool cb_called = false;
  {
    auto s = EventSubscriber{event}.Subscribe([&cb_called](int i) {
      cb_called = true;
      TEST_ASSERT_EQUAL(i, 1);
    });
    // callback must be called until s is alive
    event.Emit(1);
    TEST_ASSERT(cb_called);
    cb_called = false;
  }
  // callback must not be called because s is destroyed
  event.Emit(1);
  TEST_ASSERT(!cb_called);
}

void test_EventOnce() {
  Event<void(int)> event;
  bool cb_called = false;
  auto s = EventSubscriber{event}
               .Subscribe([&cb_called](int i) {
                 cb_called = true;
                 TEST_ASSERT_EQUAL(i, 1);
               })
               .Once();

  // callback must be called
  event.Emit(1);
  TEST_ASSERT(cb_called);
  cb_called = false;
  // callback must not be called because s is set to once
  event.Emit(1);
  TEST_ASSERT(!cb_called);
}

void test_FewSubscriptions() {
  Event<void(int)> event;
  std::vector<bool> cb_called(3, false);
  for (std::size_t i = 0; i < 3; i++) {
    auto _ = EventSubscriber{event}.Subscribe([&cb_called, i](int x) {
      cb_called[i] = true;
      TEST_ASSERT_EQUAL(x, 1);
    });
  }
  // callback must not be called because subscription is ignored
  event.Emit(1);
  for (auto b : cb_called) {
    TEST_ASSERT(!b);
  }

  auto ms = MultiSubscription{};

  for (std::size_t i = 0; i < 3; i++) {
    ms.Push(EventSubscriber{event}.Subscribe([&cb_called, i](int x) {
      cb_called[i] = true;
      TEST_ASSERT_EQUAL(x, 1);
    }));
  }
  // callback must be called because subscription is leaked and not managed
  // externally anymore
  event.Emit(1);
  for (auto b : cb_called) {
    TEST_ASSERT(b);
  }
}

void test_MultiSubscription() {
  Event<void(int)> event;
  EventSubscriber<void(int)> sub{event};
  std::vector<bool> cb_called(3, false);
  auto subscriptions_ = MultiSubscription{};

  subscriptions_.Push(sub.Subscribe([&](int x) {
    cb_called[0] = true;
    TEST_ASSERT_EQUAL(1, x);
  }),
                      sub.Subscribe([&](int x) {
                        cb_called[1] = true;
                        TEST_ASSERT_EQUAL(1, x);
                      }),
                      sub.Subscribe([&](int x) {
                        cb_called[2] = true;
                        TEST_ASSERT_EQUAL(1, x);
                      }));
  event.Emit(1);
  for (auto b : cb_called) {
    TEST_ASSERT(b);
  }
}

void test_EventRecursionCall() {
  Event<void(int)> event;
  EventSubscriber<void(int)> sub{event};

  bool cb_called_first = false;
  bool cb_called_second = false;

  auto s = sub.Subscribe([&](int x) {
    if (x == 1) {
      cb_called_first = true;
      event.Emit(2);
      return;
    }
    cb_called_second = true;
    TEST_ASSERT_EQUAL(2, x);
  });

  event.Emit(1);
  TEST_ASSERT(cb_called_first);
  TEST_ASSERT(cb_called_second);
}

void test_EventReSubscribeOnHandler() {
  Event<void(int)> event;
  EventSubscriber<void(int)> sub{event};

  bool cb_called_first = false;
  bool cb_called_second = false;

  Subscription s;

  s = sub.Subscribe([&](int x) {
    TEST_ASSERT_EQUAL(1, x);
    cb_called_first = true;
    s = sub.Subscribe([&](int x) {
      TEST_ASSERT_EQUAL(2, x);
      cb_called_second = true;
    });
  });

  event.Emit(1);
  TEST_ASSERT(cb_called_first);
  event.Emit(2);
  TEST_ASSERT(cb_called_second);
}

}  // namespace ae::test_events
int test_events() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_events::test_EventCreate);
  RUN_TEST(ae::test_events::test_EventOnce);
  RUN_TEST(ae::test_events::test_FewSubscriptions);
  RUN_TEST(ae::test_events::test_MultiSubscription);
  RUN_TEST(ae::test_events::test_EventRecursionCall);
  RUN_TEST(ae::test_events::test_EventReSubscribeOnHandler);
  return UNITY_END();
}
