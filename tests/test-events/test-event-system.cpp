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

#include <array>
#include <thread>

#include "aether/events/details/event_handler.h"
#include "aether/events/details/event_system.h"
#include "aether/events/details/generic_event_handler.h"

namespace ae::test_event_system {
using events::EventSystem;

struct TestHandler : public events::Handler<void(int)> {
  explicit TestHandler(int& i) : res{&i} {}

  void Invoke(int i) noexcept override { *res = i; }

  int* res{};
};

struct CountedHandler : public events::Handler<void()> {
  explicit CountedHandler(int& destructions) : destructions{&destructions} {}
  CountedHandler(CountedHandler&&) noexcept = default;
  ~CountedHandler() override { ++*destructions; }

  void Invoke() noexcept override {}

  int* destructions{};
};

static constexpr auto kS = sizeof(TestHandler);
static constexpr auto kA = alignof(TestHandler);
static constexpr auto kSmallHandlerStorageSize = 32;
static constexpr auto kLargeHandlerStorageSize = 64;
template <std::size_t C>
using TestEventSystem = EventSystem<C, C, kS, kA>;

void test_AddRemoveEvents() {
  auto es = TestEventSystem<5>{};

  // Reg some events
  auto k0 = es.RegEvent();
  TEST_ASSERT_TRUE(k0.has_value());
  auto k1 = es.RegEvent();
  TEST_ASSERT_TRUE(k1.has_value());

  // check if they registered
  TEST_ASSERT_TRUE(es.IsRegistered(*k0));
  TEST_ASSERT_TRUE(es.IsRegistered(*k1));

  // unregister and check if it removed
  es.UnregEvent(*k0);
  TEST_ASSERT_FALSE(es.IsRegistered(*k0));
  es.UnregEvent(*k1);
  TEST_ASSERT_FALSE(es.IsRegistered(*k1));
}

void test_AddRemoveOverflow() {
  auto es = TestEventSystem<5>{};
  // reg till overflow
  std::array<TestEventSystem<5>::Key, 5> keys{};
  for (auto i = 0; i < 5; ++i) {
    auto k = es.RegEvent();
    TEST_ASSERT_TRUE(k.has_value());
    keys[i] = *k;
  }
  // extra event could not be registered
  auto extra = es.RegEvent();
  TEST_ASSERT_FALSE(extra.has_value());

  // remove all and try again
  for (auto const& k : keys) {
    es.UnregEvent(k);
  }
  auto k6 = es.RegEvent();
  TEST_ASSERT_TRUE(k6.has_value());
}

void test_AddHandlers() {
  auto es = TestEventSystem<5>{};
  // reg one event
  auto k = es.RegEvent();
  TEST_ASSERT_TRUE(k.has_value());

  // add a handler
  int res{};
  auto* h = es.AddHandler(*k, TestHandler{res});
  TEST_ASSERT_TRUE(h != nullptr);

  // invoke handlers for event
  es.Invoke(*k, 12);
  TEST_ASSERT_EQUAL(12, res);

  // remove the handler and try invoke again
  es.RemoveHandler(*k, h);
  es.Invoke(*k, 13);
  // res should be unchanged
  TEST_ASSERT_EQUAL(12, res);

  // add handler again
  h = es.AddHandler(*k, TestHandler{res});
  TEST_ASSERT_TRUE(h != nullptr);
  // unreg event
  es.UnregEvent(*k);
  es.Invoke(*k, 14);
  // res should be unchanged
  TEST_ASSERT_EQUAL(12, res);
}

void test_HandlerCapacityFailureIsSafe() {
  constexpr int kInvocationValue = 42;
  auto es = TestEventSystem<1>{};
  auto key = es.RegEvent();
  TEST_ASSERT_TRUE(key.has_value());

  int result = 0;
  auto* first = es.AddHandler(*key, TestHandler{result});
  TEST_ASSERT_TRUE(first != nullptr);
  auto* second = es.AddHandler(*key, TestHandler{result});
  TEST_ASSERT_TRUE(second == nullptr);

  es.Invoke(*key, kInvocationValue);
  TEST_ASSERT_EQUAL(kInvocationValue, result);
}

void test_AddHandlersMultiThread() {
  auto es = TestEventSystem<100>{};

  auto k = es.RegEvent();

  std::atomic<int> invoke_counter = {};

  // the logic is
  // Add and remove handlers in cycle from different threads
  // Invoke the event from invoker thread
  auto subscribers = std::array<std::jthread, 5>{};
  for (std::size_t i = 0; i < 5; ++i) {
    subscribers[i] = std::jthread{[&](std::stop_token const& st) {
      while (!st.stop_requested()) {
        int res{};
        auto* h = es.AddHandler(*k, TestHandler{res});
        TEST_ASSERT_TRUE(h != nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        es.RemoveHandler(*k, h);
        // We are not sure if handler was invoked during sleep, so we can't test
        // res value, but check if any invoked
        if (res != int{}) {
          invoke_counter++;
        }
      }
    }};
  }

  auto invoker = std::thread([&]() {
    for (auto i = 0; i < 1000; i++) {
      es.Invoke(*k, i);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  });

  invoker.join();
  for (auto& s : subscribers) {
    s.request_stop();
    if (s.joinable()) {
      s.join();
    }
  }

  TEST_ASSERT_GREATER_THAN(5, invoke_counter.load());
}

void test_AddRegEventsMultiThread() {
  auto es = TestEventSystem<100>{};

  std::atomic<int> invoke_counter = {};
  std::array<TestEventSystem<100>::Key, 5> keys{};

  // the logic is
  // Add event, and add a handler, then remove the event
  // Invoke all events from invoker thread
  auto subscribers = std::array<std::jthread, 5>{};
  for (std::size_t i = 0; i < 5; ++i) {
    subscribers[i] = std::jthread{[&, i](std::stop_token const& st) {
      while (!st.stop_requested()) {
        int res{};
        keys[i] = es.RegEvent().value_or(0);
        auto* h = es.AddHandler(keys[i], TestHandler{res});
        TEST_ASSERT_TRUE(h != nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        es.UnregEvent(keys[i]);
        // We are not sure if handler was invoked during sleep, so we can't test
        // res value, but check if any invoked
        if (res != int{}) {
          invoke_counter++;
        }
      }
    }};
  }

  auto invoker = std::thread([&]() {
    for (auto i = 0; i < 1000; i++) {
      for (auto k : keys) {
        es.Invoke(k, i);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  });

  invoker.join();
  for (auto& s : subscribers) {
    s.request_stop();
    if (s.joinable()) {
      s.join();
    }
  }

  TEST_ASSERT_GREATER_THAN(5, invoke_counter.load());
}

void test_RecursiveInvoke() {
  using Es = EventSystem<5, 5, 32 + sizeof(events::IHandler),
                         alignof(std::max_align_t)>;
  auto es = Es{};

  auto k = es.RegEvent();
  TEST_ASSERT_TRUE(k.has_value());

  // make handler what invokes event k recursively
  int invoke_counter = 0;
  auto f = [&]() {
    if (++invoke_counter < 16) {
      es.Invoke(*k);
    }
  };

  auto* h = es.AddHandler(
      *k, events::GenericHandler<void(), decltype(f)>{std::move(f)});
  TEST_ASSERT_TRUE(h != nullptr);

  es.Invoke(*k);

  TEST_ASSERT_EQUAL(16, invoke_counter);
}

void test_InHandlerRemoveHandler() {
  using Es = EventSystem<5, 5, 32 + sizeof(events::IHandler),
                         alignof(std::max_align_t)>;
  auto es = Es{};

  auto k = es.RegEvent();
  TEST_ASSERT_TRUE(k.has_value());

  int invoke_counter = 0;

  events::IHandler* next_handler{};

  // this removes one handler from the other handler
  auto f0 = [&]() {
    TEST_ASSERT_TRUE(next_handler != nullptr);
    es.RemoveHandler(*k, next_handler);
  };

  auto* h0 = es.AddHandler(
      *k, events::GenericHandler<void(), decltype(f0)>{std::move(f0)});
  TEST_ASSERT_TRUE(h0 != nullptr);

  // we expected this to not been invoked
  auto f1 = [&]() { invoke_counter++; };
  auto* h1 = es.AddHandler(
      *k, events::GenericHandler<void(), decltype(f1)>{std::move(f1)});

  TEST_ASSERT_TRUE(h1 != nullptr);
  next_handler = h1;

  es.Invoke(*k);

  // h1 handler should not be invoked
  TEST_ASSERT_EQUAL(0, invoke_counter);
}

void test_HandlerAddedDuringEmissionWaitsForNextSnapshot() {
  using Es =
      EventSystem<1, 3, kLargeHandlerStorageSize + sizeof(events::IHandler),
                  alignof(std::max_align_t)>;
  auto es = Es{};
  auto key = es.RegEvent();
  TEST_ASSERT_TRUE(key.has_value());

  int first_calls = 0;
  int added_calls = 0;
  auto add_handler = [&]() {
    ++first_calls;
    auto added = [&]() { ++added_calls; };
    TEST_ASSERT_TRUE(
        es.AddHandler(*key, events::GenericHandler<void(), decltype(added)>{
                                added}) != nullptr);
  };
  TEST_ASSERT_TRUE(
      es.AddHandler(*key, events::GenericHandler<void(), decltype(add_handler)>{
                              add_handler}) != nullptr);

  es.Invoke(*key);
  TEST_ASSERT_EQUAL(1, first_calls);
  TEST_ASSERT_EQUAL(0, added_calls);
  es.Invoke(*key);
  TEST_ASSERT_EQUAL(2, first_calls);
  TEST_ASSERT_EQUAL(1, added_calls);
}

void test_InHandlerUnregister() {
  using Es = EventSystem<5, 5, 32 + sizeof(events::IHandler),
                         alignof(std::max_align_t)>;
  auto es = Es{};

  auto k = es.RegEvent();
  TEST_ASSERT_TRUE(k.has_value());

  int invoke_counter = 0;

  // this unregister the event from handler
  auto f0 = [&]() {
    es.UnregEvent(*k);
    es.Invoke(*k);
  };
  auto* h0 = es.AddHandler(
      *k, events::GenericHandler<void(), decltype(f0)>{std::move(f0)});
  TEST_ASSERT_TRUE(h0 != nullptr);

  // The active snapshot still invokes this handler after event retirement.
  auto f1 = [&]() { invoke_counter++; };
  auto* h1 = es.AddHandler(
      *k, events::GenericHandler<void(), decltype(f1)>{std::move(f1)});

  TEST_ASSERT_TRUE(h1 != nullptr);

  es.Invoke(*k);

  // h1 runs from the active snapshot; the recursive invoke is a no-op.
  TEST_ASSERT_EQUAL(1, invoke_counter);
  // and k event should be unregistered
  TEST_ASSERT_FALSE(es.IsRegistered(*k));
}

void test_UnregisterReclaimsHandlersAndReusesCapacity() {
  using Es =
      EventSystem<1, 1, kSmallHandlerStorageSize + sizeof(events::IHandler),
                  alignof(std::max_align_t)>;
  int destructions = 0;
  auto es = Es{};
  auto key = es.RegEvent();
  TEST_ASSERT_TRUE(key.has_value());

  auto* handler = es.AddHandler(*key, CountedHandler{destructions});
  TEST_ASSERT_TRUE(handler != nullptr);
  auto const destructions_after_temporary = destructions;

  es.UnregEvent(*key);
  TEST_ASSERT_EQUAL(destructions_after_temporary + 1, destructions);

  auto next_key = es.RegEvent();
  TEST_ASSERT_TRUE(next_key.has_value());
  auto* next_handler = es.AddHandler(*next_key, CountedHandler{destructions});
  TEST_ASSERT_TRUE(next_handler != nullptr);
}

void test_RemoveHandlerReclaimsMapRecord() {
  using Es =
      EventSystem<1, 2, kLargeHandlerStorageSize + sizeof(events::IHandler),
                  alignof(std::max_align_t)>;
  auto es = Es{};
  auto key = es.RegEvent();
  TEST_ASSERT_TRUE(key.has_value());

  int replacement_result = 0;
  bool replacement_added = false;
  events::IHandler* self = nullptr;
  auto remove_and_replace = [&]() {
    es.RemoveHandler(*key, self);
    auto temporary_callback = [] {};
    auto* temporary = es.AddHandler(
        *key, events::GenericHandler<void(), decltype(temporary_callback)>{
                  temporary_callback});
    TEST_ASSERT_TRUE(temporary != nullptr);
    es.RemoveHandler(*key, temporary);
    auto replacement = [&]() { ++replacement_result; };
    replacement_added =
        es.AddHandler(*key,
                      events::GenericHandler<void(), decltype(replacement)>{
                          replacement}) != nullptr;
  };
  self = es.AddHandler(
      *key, events::GenericHandler<void(), decltype(remove_and_replace)>{
                remove_and_replace});
  TEST_ASSERT_TRUE(self != nullptr);

  es.Invoke(*key);
  TEST_ASSERT_TRUE(replacement_added);
  es.Invoke(*key);
  TEST_ASSERT_EQUAL(1, replacement_result);
}

void test_UnregisterRetainsKeyUntilSnapshotReturns() {
  using Es =
      EventSystem<1, 2, kLargeHandlerStorageSize + sizeof(events::IHandler),
                  alignof(std::max_align_t)>;
  auto es = Es{};
  auto key = es.RegEvent();
  TEST_ASSERT_TRUE(key.has_value());

  bool key_was_reserved = false;
  auto unregister = [&]() {
    es.UnregEvent(*key);
    key_was_reserved = !es.RegEvent().has_value();
  };
  auto* handler = es.AddHandler(
      *key, events::GenericHandler<void(), decltype(unregister)>{unregister});
  TEST_ASSERT_TRUE(handler != nullptr);

  es.Invoke(*key);
  TEST_ASSERT_TRUE(key_was_reserved);
  TEST_ASSERT_TRUE(es.RegEvent().has_value());
}

void test_NestedSnapshotRetirementReclaimsAllHandlers() {
  using Es =
      EventSystem<1, 2, kLargeHandlerStorageSize + sizeof(events::IHandler),
                  alignof(std::max_align_t)>;
  auto es = Es{};
  auto key = es.RegEvent();
  TEST_ASSERT_TRUE(key.has_value());

  bool nested = false;
  bool key_was_reserved = false;
  auto retire = [&]() {
    if (!nested) {
      nested = true;
      es.Invoke(*key);
      return;
    }
    es.UnregEvent(*key);
    key_was_reserved = !es.RegEvent().has_value();
  };
  auto* handler = es.AddHandler(
      *key, events::GenericHandler<void(), decltype(retire)>{retire});
  TEST_ASSERT_TRUE(handler != nullptr);

  es.Invoke(*key);
  TEST_ASSERT_TRUE(key_was_reserved);
  TEST_ASSERT_TRUE(es.RegEvent().has_value());
}

void test_InHandlerUnregisterRemoveHandler() {
  // Unregister event from the handler and remove one of the handlers
  using Es = EventSystem<5, 5, 32 + sizeof(events::IHandler),
                         alignof(std::max_align_t)>;
  auto es = Es{};

  auto k = es.RegEvent();
  TEST_ASSERT_TRUE(k.has_value());

  int invoke_counter = 0;
  events::IHandler* next_handler{};

  // this unregisters event from handler
  auto f0 = [&]() { es.UnregEvent(*k); };
  auto* h0 = es.AddHandler(
      *k, events::GenericHandler<void(), decltype(f0)>{std::move(f0)});
  TEST_ASSERT_TRUE(h0 != nullptr);

  // This runs from the active snapshot and removes the next handler.
  auto f1 = [&]() {
    invoke_counter++;
    TEST_ASSERT_TRUE(next_handler != nullptr);
    es.RemoveHandler(*k, next_handler);
  };
  auto* h1 = es.AddHandler(
      *k, events::GenericHandler<void(), decltype(f1)>{std::move(f1)});
  TEST_ASSERT_TRUE(h1 != nullptr);

  // this handler should be removed and not been invoked
  auto f2 = [&] { invoke_counter++; };
  auto* h2 = es.AddHandler(
      *k, events::GenericHandler<void(), decltype(f2)>{std::move(f2)});
  TEST_ASSERT_TRUE(h2 != nullptr);
  next_handler = h2;

  es.Invoke(*k);

  // Event retirement preserves the snapshot, but individual unsubscription
  // still skips the not-yet-started handler.
  TEST_ASSERT_EQUAL(1, invoke_counter);
  // and k event should be unregistered
  TEST_ASSERT_FALSE(es.IsRegistered(*k));
}

}  // namespace ae::test_event_system

int test_event_system() {
  using namespace ae::test_event_system;  // NOLINT
  UNITY_BEGIN();
  RUN_TEST(test_AddRemoveEvents);
  RUN_TEST(test_AddRemoveOverflow);
  RUN_TEST(test_AddHandlers);
  RUN_TEST(test_HandlerCapacityFailureIsSafe);
  RUN_TEST(test_AddHandlersMultiThread);
  RUN_TEST(test_AddRegEventsMultiThread);
  RUN_TEST(test_RecursiveInvoke);
  RUN_TEST(test_InHandlerRemoveHandler);
  RUN_TEST(test_HandlerAddedDuringEmissionWaitsForNextSnapshot);
  RUN_TEST(test_InHandlerUnregister);
  RUN_TEST(test_InHandlerUnregisterRemoveHandler);
  RUN_TEST(test_UnregisterReclaimsHandlersAndReusesCapacity);
  RUN_TEST(test_RemoveHandlerReclaimsMapRecord);
  RUN_TEST(test_UnregisterRetainsKeyUntilSnapshotReturns);
  RUN_TEST(test_NestedSnapshotRetirementReclaimsAllHandlers);
  return UNITY_END();
}
