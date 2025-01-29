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

#include <chrono>
#include <iostream>

#include "aether/events/delegate.h"

#include "tests/test-events/worker_class.h"
#include "tests/test-events/use_delegates_class.h"

#ifndef NDEBUG
static constexpr auto kCallCount = 10'000'000;
#else
static constexpr auto kCallCount = 1000'000'000;
#endif

namespace ae::test_event_delegates {
void test_LambdaDelegate() {
  auto counter = [i{0}](int v) mutable { return i += (v % 100); };
  {
    auto time = std::chrono::steady_clock::now();
    auto delegate = Delegate{counter};
    for (volatile auto i = 0; i < kCallCount; i++) {
      volatile auto res = delegate(i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "delegate call only time diff " << (after - time).count()
              << std::endl;
  }

  {
    auto time = std::chrono::steady_clock::now();
    for (volatile auto i = 0; i < kCallCount; i++) {
      auto delegate = Delegate{counter};
      volatile auto res = delegate(i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "delegate create and call time diff " << (after - time).count()
              << std::endl;
  }
}

void test_StaticLambdaDelegate() {
  auto multiplier = [](int v) {
    auto k = (v % 100);
    return k * k;
  };
  {
    auto time = std::chrono::steady_clock::now();
    auto delegate = Delegate{multiplier};
    for (volatile auto i = 0; i < kCallCount; i++) {
      volatile auto res = delegate(i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "delegate call only time diff " << (after - time).count()
              << std::endl;
  }

  {
    auto time = std::chrono::steady_clock::now();
    for (volatile auto i = 0; i < kCallCount; i++) {
      auto delegate = Delegate{multiplier};
      volatile auto res = delegate(i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "delegate create and call time diff " << (after - time).count()
              << std::endl;
  }
}

void test_BiggerLambdaDelegate() {
  auto worker0 = test_events::WorkerClass{};
  auto worker1 = test_events::WorkerClass{};
  auto worker2 = test_events::WorkerClass{};
  auto worker3 = test_events::WorkerClass{};
  auto worker4 = test_events::WorkerClass{};
  auto counter = [i{0}, &worker0, &worker1, &worker2, &worker3,
                  &worker4](int v) mutable {
    auto k = v % 100;
    test_events::WorkerClass* used_worker;
    switch (k % 5) {
      case 0:
        used_worker = &worker0;
        break;
      case 1:
        used_worker = &worker1;
        break;
      case 2:
        used_worker = &worker2;
        break;
      case 3:
        used_worker = &worker3;
        break;
      case 4:
        used_worker = &worker4;
        break;
      default:
        return 0;
    }

    return i += used_worker->GetInt(k);
  };
  {
    auto time = std::chrono::steady_clock::now();
    auto delegate = Delegate{counter};
    for (volatile auto i = 0; i < kCallCount; i++) {
      volatile auto res = delegate(i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "delegate call only time diff " << (after - time).count()
              << std::endl;
  }

  {
    auto time = std::chrono::steady_clock::now();
    for (volatile auto i = 0; i < kCallCount; i++) {
      auto delegate = Delegate<int(int)>{counter};
      volatile auto res = delegate(i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "delegate create and call time diff " << (after - time).count()
              << std::endl;
  }
}

struct Foo {
  int Multi(int i) { return x *= (i % 100); }
  int x;
};

void test_ClassMember() {
  auto foo = Foo{2};
  {
    auto time = std::chrono::steady_clock::now();
    auto delegate = Delegate{foo, MethodPtr<&Foo::Multi>{}};
    for (volatile auto i = 0; i < kCallCount; i++) {
      volatile auto res = delegate(i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "delegate call only time diff " << (after - time).count()
              << std::endl;
  }

  {
    auto time = std::chrono::steady_clock::now();
    for (volatile auto i = 0; i < kCallCount; i++) {
      auto delegate = Delegate{foo, MethodPtr<&Foo::Multi>{}};
      volatile auto res = delegate(i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "delegate create and call time diff " << (after - time).count()
              << std::endl;
  }
}

void test_WorkerClass() {
  auto worker = test_events::WorkerClass{};

  {
    auto time = std::chrono::steady_clock::now();
    auto use_delegate = test_events::UseDelegateClass{};
    use_delegate.SetGetInt(
        Delegate{worker, MethodPtr<&test_events::WorkerClass::GetInt>{}});
    for (volatile auto i = 0; i < kCallCount; i++) {
      volatile auto res = use_delegate.Invoke(i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "delegate call only time diff " << (after - time).count()
              << std::endl;
  }

  {
    auto time = std::chrono::steady_clock::now();
    for (volatile auto i = 0; i < kCallCount; i++) {
      auto use_delegate = test_events::UseDelegateClass{};
      use_delegate.SetGetInt(
          Delegate{worker, MethodPtr<&test_events::WorkerClass::GetInt>{}});
      volatile auto res = use_delegate.Invoke(i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "delegate create and call time diff " << (after - time).count()
              << std::endl;
  }
}

}  // namespace ae::test_event_delegates

int test_event_delegates() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_event_delegates::test_LambdaDelegate);
  RUN_TEST(ae::test_event_delegates::test_StaticLambdaDelegate);
  RUN_TEST(ae::test_event_delegates::test_BiggerLambdaDelegate);
  RUN_TEST(ae::test_event_delegates::test_ClassMember);
  RUN_TEST(ae::test_event_delegates::test_WorkerClass);
  return UNITY_END();
}
