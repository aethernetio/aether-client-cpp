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

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "aether-miscpp/types/method_ptr.h"
#include "aether-miscpp/types/small_function.h"

#include "aether/events/details/event_multi_subscription.h"
#include "aether/events/details/event_object.h"
#include "aether/events/details/event_subscription.h"

namespace ae::test_events {
using events::EventObject;
using events::EventSystem;
using events::MultiSubscriptionObjectDyn;
using events::MultiSubscriptionObjectFix;
using events::SubscriptionObject;

using Es = EventSystem<5, 5, 32, alignof(std::max_align_t)>;

void test_EventObject() {
  auto es = Es{};

  // Create event object and test if it invokes subscription handler via Emit
  // multiple times
  int invoke_counter = 0;
  auto ev = EventObject<Es, void()>{es};
  ev.Subscribe([&]() { invoke_counter++; });
  TEST_ASSERT_EQUAL(0, invoke_counter);
  ev.Emit();
  TEST_ASSERT_EQUAL(1, invoke_counter);
  ev.Emit();
  TEST_ASSERT_EQUAL(2, invoke_counter);
  ev.Emit();
  TEST_ASSERT_EQUAL(3, invoke_counter);
}

void test_EventObjectSubscription() {
  using Subscription = SubscriptionObject<Es>;
  auto es = Es{};

  // Create event object and test if it invokes subscription handler via Emit
  // And resetting subscription remove handler
  int invoke_counter = 0;
  auto ev = EventObject<Es, void()>{es};
  Subscription s = ev.Subscribe([&]() { invoke_counter++; });
  TEST_ASSERT_EQUAL(0, invoke_counter);
  ev.Emit();
  TEST_ASSERT_EQUAL(1, invoke_counter);
  ev.Emit();
  TEST_ASSERT_EQUAL(2, invoke_counter);

  s.Reset();
  // after reset subscription were removed
  ev.Emit();
  TEST_ASSERT_EQUAL(2, invoke_counter);
}

void test_EventObjectMoveAssignment() {
  auto es = Es{};
  int source_calls = 0;
  int destination_calls = 0;
  auto source = EventObject<Es, void()>{es};
  auto destination = EventObject<Es, void()>{es};
  source.Subscribe([&]() { ++source_calls; });
  destination.Subscribe([&]() { ++destination_calls; });

  destination = std::move(source);
  // Verify the move assignment contract leaves the source empty.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  TEST_ASSERT_FALSE(static_cast<bool>(source));
  destination.Emit();
  TEST_ASSERT_EQUAL(1, source_calls);
  TEST_ASSERT_EQUAL(0, destination_calls);

  destination = std::move(destination);
  destination.Emit();
  TEST_ASSERT_EQUAL(2, source_calls);
}

void test_SubscriptionRaii() {
  using Subscription = SubscriptionObject<Es>;
  auto es = Es{};

  // Create event object and test if it invokes subscription handler via Emit
  // And resetting subscription remove handler
  int invoke_counter = 0;
  auto ev = EventObject<Es, void()>{es};

  {
    Subscription s = ev.Subscribe([&]() { invoke_counter++; });
    TEST_ASSERT_EQUAL(0, invoke_counter);
    ev.Emit();
    TEST_ASSERT_EQUAL(1, invoke_counter);
  }

  // after reset subscription were removed
  ev.Emit();
  TEST_ASSERT_EQUAL(1, invoke_counter);
}

void test_SubscriptionMove() {
  using Subscription = SubscriptionObject<Es>;
  auto es = Es{};

  // Test subscription move constructible and move assignable and handler stays
  // after all operations
  int invoke_counter = 0;
  auto ev = EventObject<Es, void()>{es};

  Subscription s = ev.Subscribe([&]() { invoke_counter++; });
  Subscription s1 = std::move(s);
  ev.Emit();
  TEST_ASSERT_EQUAL(1, invoke_counter);

  s = std::move(s1);
  ev.Emit();
  TEST_ASSERT_EQUAL(2, invoke_counter);

  {
    auto s2 = std::move(s);
    ev.Emit();
    TEST_ASSERT_EQUAL(3, invoke_counter);
  }
  // s2 automatically reset
  ev.Emit();
  TEST_ASSERT_EQUAL(3, invoke_counter);
}

void test_RemoveSubscriptionFromHandler() {
  using Subscription = SubscriptionObject<Es>;
  auto es = Es{};

  // Remove one subscription from another handler and test it's not invoked
  Subscription s1;
  Subscription s2;
  int s1_invoke = 0;
  int s2_invoke = 0;

  auto ev = EventObject<Es, void()>{es};
  s1 = ev.Subscribe([&]() {
    s1_invoke++;
    s2.Reset();
  });

  s2 = ev.Subscribe([&]() { s2_invoke++; });

  ev.Emit();

  TEST_ASSERT_EQUAL(1, s1_invoke);
  TEST_ASSERT_EQUAL(0, s2_invoke);
}

void test_RemoveEventInHandler() {
  using Subscription = SubscriptionObject<Es>;
  auto es = Es{};

  // Remove event from event handler
  // All subscribe handlers should be invoked

  auto opt_ev = std::optional<EventObject<Es, void()>>{es};

  int s1_invoke = 0;
  int s2_invoke = 0;
  Subscription s1 = opt_ev->Subscribe([&]() {
    s1_invoke++;
    opt_ev.reset();
  });
  Subscription s2 = opt_ev->Subscribe([&]() { s2_invoke++; });

  opt_ev->Emit();

  TEST_ASSERT_EQUAL(1, s1_invoke);
  TEST_ASSERT_EQUAL(1, s2_invoke);
  TEST_ASSERT_FALSE(opt_ev);
}

void test_DifferentSignatures() {
  using Subscription = SubscriptionObject<Es>;
  auto es = Es{};

  // create event objects with different signatures and try to emit each
  int ir1 = 0;
  int ir2 = 0;
  bool br2 = false;
  int oir3 = 0;

  auto ev1 = EventObject<Es, void(int)>{es};
  ev1.Subscribe([&](int v) { ir1 = v; });
  auto ev2 = EventObject<Es, void(int, bool)>{es};
  ev2.Subscribe([&](int v, bool f) {
    ir2 = v;
    br2 = f;
  });
  auto ev3 = EventObject<Es, void(std::optional<int>)>{es};
  ev3.Subscribe([&](std::optional<int> v) {
    if (v) {
      oir3 = *v;
    } else {
      oir3 = -1;
    }
  });

  ev1.Emit(12);
  TEST_ASSERT_EQUAL(12, ir1);
  ev2.Emit(42, true);
  TEST_ASSERT_EQUAL(42, ir2);
  TEST_ASSERT_EQUAL(br2, true);
  ev3.Emit(11);
  TEST_ASSERT_EQUAL(11, oir3);
  ev3.Emit(std::nullopt);
  TEST_ASSERT_EQUAL(-1, oir3);
}

void test_CopyablePayloadDeliveredToEveryHandler() {
  auto es = Es{};
  auto event = EventObject<Es, void(std::string)>{es};
  std::string first;
  std::string second;
  event.Subscribe([&](std::string value) { first = std::move(value); });
  event.Subscribe([&](std::string value) { second = std::move(value); });

  event.Emit("payload");
  TEST_ASSERT_EQUAL_STRING("payload", first.c_str());
  TEST_ASSERT_EQUAL_STRING("payload", second.c_str());
}

void test_FlexibleSignatures() {
  using Subscription = SubscriptionObject<Es>;
  auto es = Es{};

  // create event object and try to subscribe with handlers with different but
  // compatible signatures

  int ir1 = 0;
  auto ev1 = EventObject<Es, void(int const&)>{es};
  ev1.Subscribe([&](int v) { ir1 = v; });
  ev1.Emit(12);
  TEST_ASSERT_EQUAL(12, ir1);

  int res2 = 0;
  auto ev2 = EventObject<Es, void(std::vector<int>)>{es};
  ev2.Subscribe([&](std::vector<int>&& v) {
    res2 = static_cast<int>(std::move(v).size());
  });
  ev2.Emit(std::vector{1, 2, 3, 4});
  TEST_ASSERT_EQUAL(4, res2);

  struct Base {};
  struct Derived : public Base {};
  Base const* b{};
  auto ev3 = EventObject<Es, void(Derived const&)>{es};
  ev3.Subscribe([&](Base const& v) { b = &v; });
  auto d = Derived{};
  ev3.Emit(d);
  TEST_ASSERT_EQUAL(&d, b);
}

void test_SpecialHandlers() {
  using Subscription = SubscriptionObject<Es>;
  auto es = Es{};

  // create event objects and try to use not generic but special handlers
  auto ev1 = EventObject<Es, void(int)>{es};

  // define special handler for this signature
  struct H1 : public events::Handler<void(int)> {
    explicit H1(int& r) : r_{&r} {}
    void Invoke(int v) noexcept override { *r_ = v; }
    int* r_{};
  };

  int res = 0;
  ev1.Subscribe(H1{res});

  ev1.Emit(654);
  TEST_ASSERT_EQUAL(654, res);
}

void test_MultiSubscriptions() {
  using Es = EventSystem<10, 10, 32, alignof(std::max_align_t)>;
  using Subscription = SubscriptionObject<Es>;
  auto es = Es{};

  // Create different event objects and subscribe to then using one
  // multisubscription

  auto msub = MultiSubscriptionObjectDyn<Es>{};

  int res1 = 0;
  auto ev1 = EventObject<Es, void(int)>{es};
  // common way to add one subscription
  msub += ev1.Subscribe([&](int v) { res1 = v; });

  bool res2 = false;
  auto ev2 = EventObject<Es, void(bool)>{es};
  // another way to add subscription
  msub.Push(ev2.Subscribe([&](bool v) { res2 = v; }));

  int res3 = 0;
  auto ev3 = EventObject<Es, void()>{es};
  int res4 = 0;
  auto ev4 = EventObject<Es, void(std::vector<int> const&)>{es};
  int ires5 = 0;
  bool bres5 = false;
  auto ev5 = EventObject<Es, void(int, bool)>{es};

  // add multiple subscription at once
  msub.Push(ev3.Subscribe([&]() { res3 = 1; }),
            ev4.Subscribe([&](std::vector<int> const& v) {
              res4 = static_cast<int>(v.size());
            }),
            ev5.Subscribe([&](int v, bool f) {
              ires5 = v;
              bres5 = f;
            }));

  ev1.Emit(12);
  ev2.Emit(true);
  ev3.Emit();
  ev4.Emit(std::vector{1, 23, 3});
  ev5.Emit(55, true);

  TEST_ASSERT_EQUAL(12, res1);
  TEST_ASSERT_EQUAL(true, res2);
  TEST_ASSERT_EQUAL(1, res3);
  TEST_ASSERT_EQUAL(3, res4);
  TEST_ASSERT_EQUAL(55, ires5);
  TEST_ASSERT_EQUAL(true, bres5);

  // unsubscribe from all at once
  msub.Reset();
  res1 = 0;
  ev1.Emit(12);
  // did not invoked
  TEST_ASSERT_EQUAL(0, res1);
}

void test_MultiSubscriptionMoveAssignment() {
  using Multi = MultiSubscriptionObjectDyn<Es>;
  auto first_system = Es{};
  auto second_system = Es{};
  auto first_event = EventObject<Es, void()>{first_system};
  auto second_event = EventObject<Es, void()>{second_system};
  int first_calls = 0;
  int second_calls = 0;
  Multi destination;
  destination += first_event.Subscribe([&]() { ++first_calls; });
  Multi source;
  source += second_event.Subscribe([&]() { ++second_calls; });

  destination = std::move(source);
  // Verify the move assignment contract leaves the source empty.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  TEST_ASSERT_FALSE(static_cast<bool>(source));
  first_event.Emit();
  second_event.Emit();
  TEST_ASSERT_EQUAL(0, first_calls);
  TEST_ASSERT_EQUAL(1, second_calls);

  source += first_event.Subscribe([&]() { ++first_calls; });
  source.Reset();
  first_event.Emit();
  TEST_ASSERT_EQUAL(0, first_calls);
}

void test_FixedMultiSubscription() {
  using Multi = MultiSubscriptionObjectFix<Es, 2>;
  auto es = Es{};
  auto first_event = EventObject<Es, void()>{es};
  auto second_event = EventObject<Es, void()>{es};
  int calls = 0;
  Multi subscriptions;
  subscriptions.Push(first_event.Subscribe([&]() { ++calls; }),
                     second_event.Subscribe([&]() { ++calls; }));

  first_event.Emit();
  second_event.Emit();
  TEST_ASSERT_EQUAL(2, calls);
  subscriptions.Reset();
  first_event.Emit();
  TEST_ASSERT_EQUAL(2, calls);
}

void test_EventSubscribeToCallable() {
  using Es = EventSystem<5, 5, 64, alignof(std::max_align_t)>;
  using Subscription = SubscriptionObject<Es>;
  auto es = Es{};

  // subscribe to event with different callables
  auto ev = EventObject<Es, void(int)>{es};

  // Small function
  int res1 = 0;
  ev.Subscribe(SmallFunction<void(int), 8, 8>{[&](int v) { res1 = v; }});

  // MethodPtr
  struct Test {
    void Invoke(int v) { res = v; }

    int res{};
  };
  Test test;
  ev.Subscribe(MethodPtr<&Test::Invoke>{&test});

  // std::function
  int res2 = 0;
  ev.Subscribe(std::function<void(int)>{[&](int v) { res2 = v; }});

  // ref to std::function
  int res3 = 0;
  auto f = std::function<void(int)>{[&](int v) { res3 = v; }};
  ev.Subscribe(f);
  // replace value on ref
  int res4 = 0;
  f = std::function<void(int)>{[&](int v) { res4 = v; }};

  // all handlers get the same value
  ev.Emit(341);

  TEST_ASSERT_EQUAL(341, res1);
  TEST_ASSERT_EQUAL(341, test.res);
  TEST_ASSERT_EQUAL(341, res2);
  TEST_ASSERT_EQUAL(0, res3);
  TEST_ASSERT_EQUAL(341, res4);
}

template <typename F>
using GH = events::GenericHandler<void(int), F>;

struct Callable {
  static inline int copy_count = 0;
  static inline int move_count = 0;

  Callable() = default;
  Callable(Callable const&) { copy_count++; }
  Callable(Callable&&) noexcept { move_count++; }

  void operator()(int) const {}
};

void test_GenericHandler() {
  {
    // Test no copy made
    Callable::copy_count = 0;
    Callable::move_count = 0;

    auto gh = GH<Callable>{Callable{}};
    TEST_ASSERT_EQUAL(0, Callable::copy_count);
    TEST_ASSERT_EQUAL(1, Callable::move_count);
    auto gh1 = std::move(gh);
    TEST_ASSERT_EQUAL(0, Callable::copy_count);
    TEST_ASSERT_EQUAL(2, Callable::move_count);
  }
  {
    // Test take reference, no copy, no move
    Callable::copy_count = 0;
    Callable::move_count = 0;

    Callable clbl;
    auto gh = GH<Callable&>{clbl};
    TEST_ASSERT_EQUAL(0, Callable::copy_count);
    TEST_ASSERT_EQUAL(0, Callable::move_count);
    auto gh1 = std::move(gh);
    TEST_ASSERT_EQUAL(0, Callable::copy_count);
    TEST_ASSERT_EQUAL(0, Callable::move_count);
  }
  {
    // Test take const reference, no copy, no move
    Callable::copy_count = 0;
    Callable::move_count = 0;

    Callable clbl;
    auto gh = GH<Callable const&>{clbl};
    TEST_ASSERT_EQUAL(0, Callable::copy_count);
    TEST_ASSERT_EQUAL(0, Callable::move_count);
    auto gh1 = std::move(gh);
    TEST_ASSERT_EQUAL(0, Callable::copy_count);
    TEST_ASSERT_EQUAL(0, Callable::move_count);
  }
  {
    // Test take &&ref, no copy, no move
    Callable::copy_count = 0;
    Callable::move_count = 0;

    Callable clbl;
    auto gh = GH<Callable&&>{std::move(clbl)};
    TEST_ASSERT_EQUAL(0, Callable::copy_count);
    TEST_ASSERT_EQUAL(0, Callable::move_count);
    auto gh1 = std::move(gh);
    TEST_ASSERT_EQUAL(0, Callable::copy_count);
    TEST_ASSERT_EQUAL(0, Callable::move_count);
  }
  {
    // Test make one intentional copy
    Callable::copy_count = 0;
    Callable::move_count = 0;

    Callable clbl;
    auto gh = GH<Callable>{clbl};
    TEST_ASSERT_EQUAL(1, Callable::copy_count);
    TEST_ASSERT_EQUAL(0, Callable::move_count);
    auto gh1 = std::move(gh);
    TEST_ASSERT_EQUAL(1, Callable::copy_count);
    TEST_ASSERT_EQUAL(1, Callable::move_count);
  }
}

}  // namespace ae::test_events

int test_events() {
  using namespace ae::test_events;  // NOLINT
  UNITY_BEGIN();
  RUN_TEST(test_EventObject);
  RUN_TEST(test_EventObjectSubscription);
  RUN_TEST(test_EventObjectMoveAssignment);
  RUN_TEST(test_SubscriptionRaii);
  RUN_TEST(test_SubscriptionMove);
  RUN_TEST(test_RemoveSubscriptionFromHandler);
  RUN_TEST(test_RemoveEventInHandler);
  RUN_TEST(test_DifferentSignatures);
  RUN_TEST(test_CopyablePayloadDeliveredToEveryHandler);
  RUN_TEST(test_FlexibleSignatures);
  RUN_TEST(test_SpecialHandlers);
  RUN_TEST(test_MultiSubscriptions);
  RUN_TEST(test_MultiSubscriptionMoveAssignment);
  RUN_TEST(test_FixedMultiSubscription);
  RUN_TEST(test_EventSubscribeToCallable);
  RUN_TEST(test_GenericHandler);
  return UNITY_END();
}
