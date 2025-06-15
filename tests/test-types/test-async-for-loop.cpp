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

#include <vector>
#include <utility>
#include <optional>
#include <string_view>

#include "aether/types/async_for_loop.h"

namespace ae::test_async_for_loop {
void test_RawAsyncForLoop() {
  int i;
  int count = 10;
  auto async_for_loop =
      AsyncForLoop<int>{[&]() { i = 0; }, [&]() { return i < count; },
                        [&]() { ++i; }, [&]() { return i; }};

  for (auto j = 0; j < count; ++j) {
    auto k = async_for_loop.Update();
    TEST_ASSERT(k.has_value());
    TEST_ASSERT_EQUAL(j, *k);
  }
  auto k = async_for_loop.Update();
  TEST_ASSERT(!k.has_value());
  TEST_ASSERT_EQUAL(count, i);
}

struct IntGenerator {
  explicit IntGenerator(int from, int to, int step)
      : from_{from}, to_{to}, step_{step} {}

  void Init() { value_ = from_; }
  bool End() const { return value_ >= to_; }
  void Next() { value_ += step_; }
  int Get() const { return value_; }

  int value_;
  int from_;
  int to_;
  int step_;
};

void test_GeneratorAsyncForLoop() {
  auto generator = IntGenerator{0, 10, 2};
  auto async_for_loop = AsyncForLoop<int>::Construct(generator);

  int count{};
  std::optional<int> k;
  while ((k = async_for_loop.Update(), k.has_value())) {
    count++;
  }
  TEST_ASSERT_EQUAL(5, count);
}

struct StringGenerator {
  explicit StringGenerator(std::vector<std::string_view> strings)
      : strings_{std::move(strings)} {}

  void Init() { iter_ = std::begin(strings_); }
  bool End() const { return iter_ == std::end(strings_); }
  void Next() { ++iter_; }
  std::string_view const& Get() const { return *iter_; }

  std::vector<std::string_view> strings_;
  std::vector<std::string_view>::iterator iter_;
};

void test_StringGeneratorAsyncForLoop() {
  auto generator = StringGenerator{{"hello", "world", "!"}};
  auto async_for_loop = AsyncForLoop<const char*>::Construct(
      generator, [&generator]() { return generator.Get().data(); });

  char const* k;
  int count{};
  while ((k = async_for_loop.Update(), k != nullptr)) {
    TEST_ASSERT_EQUAL_STRING(generator.strings_[count].data(), k);
    ++count;
  }
  TEST_ASSERT_EQUAL(3, count);
}

}  // namespace ae::test_async_for_loop

int test_async_for_loop() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_async_for_loop::test_RawAsyncForLoop);
  RUN_TEST(ae::test_async_for_loop::test_GeneratorAsyncForLoop);
  RUN_TEST(ae::test_async_for_loop::test_StringGeneratorAsyncForLoop);
  return UNITY_END();
}
