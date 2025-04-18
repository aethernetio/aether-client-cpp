/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/ptr/storage.h"

namespace ae::test_storage {
struct CopyMoveCounter {
  static inline int delete_count = 0;

  CopyMoveCounter() = default;
  ~CopyMoveCounter() { delete_count++; }

  CopyMoveCounter(CopyMoveCounter const& other)
      : copy_count{other.copy_count + 1}, move_count{other.move_count} {}
  CopyMoveCounter(CopyMoveCounter&& other) noexcept
      : copy_count{other.copy_count}, move_count{other.move_count + 1} {}

  CopyMoveCounter& operator=(CopyMoveCounter const& other) {
    if (this != &other) {
      copy_count = other.copy_count + 1;
      move_count = other.move_count;
    }
    return *this;
  }

  CopyMoveCounter& operator=(CopyMoveCounter&& other) noexcept {
    if (this != &other) {
      copy_count = other.copy_count;
      move_count = other.move_count + 1;
    }
    return *this;
  }

  int copy_count{};
  int move_count{};
};

void test_CopyableStorage() {
  CopyMoveCounter::delete_count = 0;

  {
    using Storage =
        CopyableStorage<sizeof(CopyMoveCounter), alignof(CopyMoveCounter)>;

    Storage s{CopyMoveCounter{}};
    TEST_ASSERT_EQUAL(1, s.ptr<CopyMoveCounter>()->move_count);

    auto s1 = s;
    TEST_ASSERT_EQUAL(1, s1.ptr<CopyMoveCounter>()->copy_count);
    TEST_ASSERT_EQUAL(1, s1.ptr<CopyMoveCounter>()->move_count);
    auto s2 = s1;
    TEST_ASSERT_EQUAL(2, s2.ptr<CopyMoveCounter>()->copy_count);
    TEST_ASSERT_EQUAL(1, s2.ptr<CopyMoveCounter>()->move_count);
    auto s3 = std::move(s2);
    TEST_ASSERT_EQUAL(2, s3.ptr<CopyMoveCounter>()->copy_count);
    TEST_ASSERT_EQUAL(2, s3.ptr<CopyMoveCounter>()->move_count);
    s2 = std::move(s3);
    TEST_ASSERT_EQUAL(2, s2.ptr<CopyMoveCounter>()->copy_count);
    TEST_ASSERT_EQUAL(3, s2.ptr<CopyMoveCounter>()->move_count);
  }

  TEST_ASSERT_EQUAL(6, CopyMoveCounter::delete_count);
}
}  // namespace ae::test_storage

int test_storage() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_storage::test_CopyableStorage);
  return UNITY_END();
}
