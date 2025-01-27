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
#include <vector>
#include <iostream>
#include <functional>

static constexpr auto kCallCount = 1000000000;

namespace ae::test_std_function {
void test_LambdaCall() {
  auto counter = [i{0}](int v) mutable { return i += (v % 100); };
  {
    auto time = std::chrono::steady_clock::now();
    auto func = std::function<int(int)>{counter};
    for (volatile auto i = 0; i < kCallCount; i++) {
      volatile auto res = func(i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "std::function call only time diff " << (after - time).count()
              << std::endl;
  }

  auto counter2 = [x{0}](int v) mutable { return x += (v % 100); };
  {
    auto time = std::chrono::steady_clock::now();
    for (auto i = 0; i < kCallCount; i++) {
      auto func = std::function<int(int)>(counter2);
      volatile auto res = func(i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "std::function create and call time diff "
              << (after - time).count() << std::endl;
  }
}

struct Foo {
  int Multi(int i) { return x *= (i % 100); }
  int x;
};
void test_ClassMember() {
  auto foo1 = Foo{2};
  {
    auto time = std::chrono::steady_clock::now();
    auto func = std::function<int(Foo*, int)>{&Foo::Multi};
    for (volatile auto i = 0; i < kCallCount; i++) {
      volatile auto res = func(&foo1, i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "std::function call only time diff " << (after - time).count()
              << std::endl;
  }

  auto foo2 = Foo{2};
  {
    auto time = std::chrono::steady_clock::now();
    for (volatile auto i = 0; i < kCallCount; i++) {
      auto func = std::function<int(Foo*, int)>{&Foo::Multi};
      volatile auto res = func(&foo2, i);
    }
    auto after = std::chrono::steady_clock::now();
    std::cout << "std::function create and call time diff "
              << (after - time).count() << std::endl;
  }
}
}  // namespace ae::test_std_function

int test_std_function() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_std_function::test_LambdaCall);
  RUN_TEST(ae::test_std_function::test_ClassMember);
  return UNITY_END();
}
