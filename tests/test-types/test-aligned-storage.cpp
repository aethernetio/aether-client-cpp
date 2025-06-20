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

#include <cstddef>

#include "aether/types/aligned_storage.h"

namespace ae::test_aligned_storage {

void test_AlignedStorage() {
  // Test basic size and alignment properties
  {
    using TestStorage = AlignedStorage<64, 16>;
    TestStorage storage;

    // Verify size matches
    TEST_ASSERT_EQUAL(64, sizeof(storage));

    // Verify alignment
    auto address = reinterpret_cast<uintptr_t>(storage.data());
    TEST_ASSERT_TRUE_MESSAGE(address % 16 == 0, "Alignment not satisfied");
  }

  // Test struct with alignment requirements
  {
    struct alignas(32) AlignedStruct {
      int data[4];
    };

    using AlignedStructStorage =
        AlignedStorage<sizeof(AlignedStruct), alignof(AlignedStruct)>;
    AlignedStructStorage aligned_struct_storage;

    // Test that the storage doesn't add extra padding
    TEST_ASSERT_EQUAL(sizeof(AlignedStruct), sizeof(aligned_struct_storage));

    auto* struct_ptr = aligned_struct_storage.ptr<AlignedStruct>();
    struct_ptr->data[0] = 1.0;
    TEST_ASSERT_EQUAL_DOUBLE(1.0, struct_ptr->data[0]);

    // Verify alignment of custom struct
    auto struct_address = reinterpret_cast<uintptr_t>(struct_ptr);
    TEST_ASSERT_TRUE_MESSAGE(struct_address % 32 == 0,
                             "Custom alignment not satisfied");
  }
}

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

void test_ManagedStorage() {
  CopyMoveCounter::delete_count = 0;

  {
    using Storage =
        ManagedStorage<sizeof(CopyMoveCounter), alignof(CopyMoveCounter)>;

    Storage s{InPlace<CopyMoveCounter>{}};
    TEST_ASSERT_EQUAL(0, s.ptr<CopyMoveCounter>()->move_count);
    TEST_ASSERT_EQUAL(0, s.ptr<CopyMoveCounter>()->copy_count);

    // make copy
    auto s1 = s;
    TEST_ASSERT_EQUAL(1, s1.ptr<CopyMoveCounter>()->copy_count);
    TEST_ASSERT_EQUAL(0, s1.ptr<CopyMoveCounter>()->move_count);
    // make copy 2
    auto s2 = s1;
    TEST_ASSERT_EQUAL(2, s2.ptr<CopyMoveCounter>()->copy_count);
    TEST_ASSERT_EQUAL(0, s2.ptr<CopyMoveCounter>()->move_count);
    // move to new storage
    auto s3 = std::move(s2);
    TEST_ASSERT_EQUAL(2, s3.ptr<CopyMoveCounter>()->copy_count);
    TEST_ASSERT_EQUAL(1, s3.ptr<CopyMoveCounter>()->move_count);
    // move to existing storage
    s2 = std::move(s3);
    TEST_ASSERT_EQUAL(2, s2.ptr<CopyMoveCounter>()->copy_count);
    TEST_ASSERT_EQUAL(2, s2.ptr<CopyMoveCounter>()->move_count);
  }

  TEST_ASSERT_EQUAL(3, CopyMoveCounter::delete_count);
}

struct NonCopyable {
  inline static int delete_count = 0;

  explicit NonCopyable(int v) : value(v) {}
  ~NonCopyable() { delete_count++; }

  // Delete copy operations
  NonCopyable(const NonCopyable&) = delete;
  NonCopyable& operator=(const NonCopyable&) = delete;

  // Allow move operations
  NonCopyable(NonCopyable&& other) noexcept : value(other.value) {
    other.value = -1;
  }

  NonCopyable& operator=(NonCopyable&& other) noexcept {
    value = other.value;
    other.value = -1;
    return *this;
  }

  int value;
};

void test_ManagedStorageNonCopyable() {
  NonCopyable::delete_count = 0;

  // Test in-place construction
  {
    auto storage = ManagedStorage<sizeof(NonCopyable), alignof(NonCopyable)>{
        InPlace<NonCopyable>{}, 42};
    auto* ptr = storage.ptr<NonCopyable>();
    TEST_ASSERT_EQUAL(42, ptr->value);

    // Test move construction
    auto moved = std::move(storage);
    auto moved_ptr = moved.ptr<NonCopyable>();
    TEST_ASSERT_EQUAL(42, moved_ptr->value);

    // Original should be in moved-from state
    TEST_ASSERT_EQUAL(-1, storage.ptr<NonCopyable>()->value);
  }
  TEST_ASSERT_EQUAL(
      1, NonCopyable::delete_count);  // Both storage and moved destroyed

  // Test move assignment
  NonCopyable::delete_count = 0;
  {
    ManagedStorage<sizeof(NonCopyable), alignof(NonCopyable)> source{
        InPlace<NonCopyable>{}, 100};
    ManagedStorage<sizeof(NonCopyable), alignof(NonCopyable)> target;

    target = std::move(source);
    auto* target_ptr = target.ptr<NonCopyable>();
    TEST_ASSERT_EQUAL(100, target_ptr->value);
    TEST_ASSERT_EQUAL(-1, source.ptr<NonCopyable>()->value);
  }
  TEST_ASSERT_EQUAL(
      1, NonCopyable::delete_count);  // Both source and target destroyed

  // Test self-move assignment (should be safe)
  NonCopyable::delete_count = 0;
  {
    ManagedStorage<sizeof(NonCopyable), alignof(NonCopyable)> storage{
        InPlace<NonCopyable>{}, 200};

    // Self-move assignment
    storage = std::move(storage);

    // Should still be valid
    auto* ptr = storage.ptr<NonCopyable>();
    TEST_ASSERT_EQUAL(200, ptr->value);
  }
  TEST_ASSERT_EQUAL(1, NonCopyable::delete_count);
}

// Common polymorphic types for tests
struct TestBase {
  virtual ~TestBase() = default;
  virtual int id() const = 0;
  virtual const char* name() const = 0;
};

struct TestDerived : TestBase {
  TestDerived(int id, const char* name) : id_(id), name_(name) {}
  int id() const override { return id_; }
  const char* name() const override { return name_; }
  int id_;
  const char* name_;
};

void test_ManagedStorageAdvanced() {
  // Test polymorphic type support
  {
    // Store derived type
    ManagedStorage<sizeof(TestDerived), alignof(TestDerived)> storage{
        InPlace<TestDerived>{}, 42, "42"};

    // Access via base pointer
    auto* base_ptr = storage.ptr<TestBase>();
    TEST_ASSERT_EQUAL(42, base_ptr->id());

    // Verify vtable preservation through moves
    auto moved = std::move(storage);
    auto* moved_base = moved.ptr<TestBase>();
    TEST_ASSERT_EQUAL(42, moved_base->id());
  }

  // Test trivially copyable type
  {
    struct PodType {
      int id;
      char const* name;
    };

    ManagedStorage<sizeof(PodType), alignof(PodType)> storage{
        InPlace<PodType>{}, PodType{1, "Test Pod"}};
    auto* ptr = storage.ptr<PodType>();
    TEST_ASSERT_EQUAL(1, ptr->id);
    TEST_ASSERT_EQUAL_STRING("Test Pod", ptr->name);

    // Test copy assignment
    auto copy = storage;
    auto* copy_ptr = copy.ptr<PodType>();
    TEST_ASSERT_EQUAL(1, copy_ptr->id);
    TEST_ASSERT_EQUAL_STRING("Test Pod", copy_ptr->name);

    // Modify original and verify copy unchanged
    ptr->id = 2;
    ptr->name = "Modified";
    TEST_ASSERT_EQUAL(2, ptr->id);
    TEST_ASSERT_EQUAL_STRING("Modified", ptr->name);
    TEST_ASSERT_EQUAL(1, copy_ptr->id);
    TEST_ASSERT_EQUAL_STRING("Test Pod", copy_ptr->name);
  }

  // Test assignment to non-empty storage
  {
    ManagedStorage<sizeof(int), alignof(int)> storage{InPlace<int>{}, 42};
    ManagedStorage<sizeof(int), alignof(int)> other{InPlace<int>{}, 100};

    // Assign to non-empty storage
    storage = other;
    TEST_ASSERT_EQUAL(100, *storage.ptr<int>());

    // Self-assignment
    storage = storage;
    TEST_ASSERT_EQUAL(100, *storage.ptr<int>());

    // Move assignment to non-empty
    storage = ManagedStorage<sizeof(int), alignof(int)>{InPlace<int>{}, 200};
    TEST_ASSERT_EQUAL(200, *storage.ptr<int>());
  }

  // Test type with larger alignment
  {
    struct alignas(64) OverAligned {
      const char* data;
    };

    ManagedStorage<sizeof(OverAligned), alignof(OverAligned)> storage{
        InPlace<OverAligned>{}, OverAligned{"Aligned Data"}};
    auto* ptr = storage.ptr<OverAligned>();
    auto address = reinterpret_cast<uintptr_t>(ptr);
    TEST_ASSERT_TRUE_MESSAGE(address % 64 == 0,
                             "64-byte alignment not satisfied");
    TEST_ASSERT_EQUAL_STRING("Aligned Data", ptr->data);
  }
}

void test_ManagedStorageEmplace() {
  // Test basic Emplace
  {
    struct ValueType {
      int a;
      const char* b;

      ValueType(int x, const char* y) : a(x), b(y) {}
    };

    ManagedStorage<sizeof(ValueType), alignof(ValueType)> storage;
    storage.Emplace<ValueType>(42, "Emplace Test");

    auto* ptr = storage.ptr<ValueType>();
    TEST_ASSERT_EQUAL(42, ptr->a);
    TEST_ASSERT_EQUAL_STRING("Emplace Test", ptr->b);
  }

  // Test Emplace with movable-only type
  {
    ManagedStorage<sizeof(NonCopyable), alignof(NonCopyable)> storage;
    storage.Emplace<NonCopyable>(100);

    auto* ptr = storage.ptr<NonCopyable>();
    TEST_ASSERT_EQUAL(100, ptr->value);
  }

  // Test Emplace overwrite
  {
    ManagedStorage<sizeof(int), alignof(int)> storage{InPlace<int>{}, 42};
    storage.Emplace<int>(100);

    auto* ptr = storage.ptr<int>();
    TEST_ASSERT_EQUAL(100, *ptr);
  }

  // Test Emplace with polymorphic type
  {
    ManagedStorage<sizeof(TestDerived), alignof(TestDerived)> storage;
    storage.Emplace<TestDerived>(123, "Polymorphic Emplace");

    auto* base_ptr = storage.ptr<TestBase>();
    TEST_ASSERT_EQUAL(123, base_ptr->id());
    TEST_ASSERT_EQUAL_STRING("Polymorphic Emplace", base_ptr->name());
  }

  // Test Emplace with different constructor
  {
    struct MultiArg {
      int a;
      int b;
      const char* c;
      MultiArg(int x, int y, const char* z) : a(x), b(y), c(z) {}
    };

    ManagedStorage<sizeof(MultiArg), alignof(MultiArg)> storage;
    storage.Emplace<MultiArg>(1, 2, "Three");

    auto* ptr = storage.ptr<MultiArg>();
    TEST_ASSERT_EQUAL(1, ptr->a);
    TEST_ASSERT_EQUAL(2, ptr->b);
    TEST_ASSERT_EQUAL_STRING("Three", ptr->c);
  }
}

void test_ManagedStorageEdgeCases() {
  // Test minimal size storage
  {
    struct Empty {};
    static_assert(sizeof(Empty) == 1, "Empty struct must be 1 byte");

    // AlignedStorage with minimal size
    AlignedStorage<1, 1> minimal_storage;
    auto* empty_ptr = minimal_storage.ptr<Empty>();
    TEST_ASSERT_NOT_NULL(empty_ptr);  // Should be valid pointer

    // ManagedStorage with minimal size
    ManagedStorage<1, 1> managed_minimal{InPlace<Empty>{}};
    auto* managed_minimal_ptr = managed_minimal.ptr<Empty>();
    TEST_ASSERT_NOT_NULL(managed_minimal_ptr);

    // Test move operations
    auto moved_minimal = std::move(managed_minimal);
    TEST_ASSERT_NOT_NULL(moved_minimal.ptr<Empty>());
  }

  // Test nested storage
  {
    using InnerStorage = AlignedStorage<sizeof(int), alignof(int)>;
    using OuterStorage =
        AlignedStorage<sizeof(InnerStorage), alignof(InnerStorage)>;

    // Test with ManagedStorage
    ManagedStorage<sizeof(OuterStorage), alignof(OuterStorage)> managed_outer{
        InPlace<OuterStorage>{}};
    auto* managed_inner_ptr =
        managed_outer.ptr<OuterStorage>()->ptr<InnerStorage>();
    int* managed_value_ptr = managed_inner_ptr->ptr<int>();
    TEST_ASSERT_NOT_NULL(managed_value_ptr);
  }

  // Test storage with maximum alignment
  {
    constexpr size_t max_align = alignof(std::max_align_t);
    struct MaxAligned {
      char data;
    };

    AlignedStorage<sizeof(MaxAligned), max_align> storage;
    auto* ptr = storage.ptr<MaxAligned>();
    auto address = reinterpret_cast<uintptr_t>(ptr);
    TEST_ASSERT_TRUE_MESSAGE(address % max_align == 0,
                             "Max alignment not satisfied");
  }
}

void test_Storage() {
  // Test with primitive type
  using IntStorage = Storage<int>;
  IntStorage int_storage;
  new (int_storage.ptr()) int{42};
  TEST_ASSERT_EQUAL(42, *int_storage.ptr());

  // Test with custom type
  struct TestStruct {
    int a;
    const char* b;
  };
  using StructStorage = Storage<TestStruct>;
  StructStorage struct_storage;
  new (struct_storage.ptr()) TestStruct();
  struct_storage.ptr()->a = 10;
  struct_storage.ptr()->b = "Hello";
  TEST_ASSERT_EQUAL(10, struct_storage.ptr()->a);
  TEST_ASSERT_EQUAL_STRING("Hello", struct_storage.ptr()->b);
}

}  // namespace ae::test_aligned_storage

int test_aligned_storage() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_aligned_storage::test_AlignedStorage);
  RUN_TEST(ae::test_aligned_storage::test_ManagedStorage);
  RUN_TEST(ae::test_aligned_storage::test_ManagedStorageNonCopyable);
  RUN_TEST(ae::test_aligned_storage::test_ManagedStorageAdvanced);
  RUN_TEST(ae::test_aligned_storage::test_ManagedStorageEmplace);
  RUN_TEST(ae::test_aligned_storage::test_ManagedStorageEdgeCases);
  RUN_TEST(ae::test_aligned_storage::test_Storage);
  return UNITY_END();
}
