/*
 * Copyright 2025 Aethernet Inc.
 */

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include <unity.h>

#include "aether/ptr/ref_tree.h"

namespace ae::test_ref_tree {

std::uintptr_t MakePointer(std::size_t offset) noexcept {
  return static_cast<std::uintptr_t>(0x1000u + offset);
}

bool ContainsIndex(std::vector<RefTree::Index> const& values,
                   RefTree::Index index) noexcept {
  for (auto const value : values) {
    if (value == index) {
      return true;
    }
  }
  return false;
}

struct MoveOnlyVisitor {
  explicit MoveOnlyVisitor(std::vector<RefTree::Index>& visited) noexcept
      : visited{&visited} {}
  MoveOnlyVisitor(MoveOnlyVisitor const&) = delete;
  MoveOnlyVisitor& operator=(MoveOnlyVisitor const&) = delete;
  MoveOnlyVisitor(MoveOnlyVisitor&&) noexcept = default;
  MoveOnlyVisitor& operator=(MoveOnlyVisitor&&) noexcept = default;
  bool operator()(RefTree::Node& node) noexcept {
    visited->push_back(node.index);
    return true;
  }
  std::vector<RefTree::Index>* visited{};
};

static_assert(!std::is_copy_constructible_v<RefTree>);
static_assert(!std::is_copy_assignable_v<RefTree>);
static_assert(!std::is_nothrow_move_constructible_v<RefTree>);
static_assert(!std::is_nothrow_move_assignable_v<RefTree>);
static_assert(!noexcept(
    std::declval<RefTree::Node&>().ForEach(std::declval<MoveOnlyVisitor&>())));
static_assert(!noexcept(std::declval<RefTree::Node&>().IsReachable(
    std::declval<RefTree::Index>())));

void test_RefTreeEmplaceDeduplicatesAndPreservesNodeData() {
  auto tree = RefTree{};
  auto& first = tree.Emplace(MakePointer(0), 2);
  auto& second = tree.Emplace(MakePointer(1), 3);

  TEST_ASSERT_EQUAL_UINT16(0, first.index);
  TEST_ASSERT_EQUAL(static_cast<std::uint64_t>(MakePointer(0)),
                    static_cast<std::uint64_t>(first.value.pointer));
  TEST_ASSERT_EQUAL_UINT16(2, first.value.ref_count);
  TEST_ASSERT_EQUAL_UINT16(0, first.value.reachable_ref_count);
  TEST_ASSERT_EQUAL_UINT16(0, first.child_count);

  TEST_ASSERT_EQUAL_UINT16(1, second.index);
  TEST_ASSERT_EQUAL(static_cast<std::uint64_t>(MakePointer(1)),
                    static_cast<std::uint64_t>(second.value.pointer));
  TEST_ASSERT_EQUAL_UINT16(3, second.value.ref_count);
  TEST_ASSERT_EQUAL_UINT16(0, second.value.reachable_ref_count);
  TEST_ASSERT_EQUAL_UINT16(0, second.child_count);

  first.PushChild(second.index);
  first.value.reachable_ref_count = 1;

  auto& duplicate = tree.Emplace(MakePointer(0), 9);
  TEST_ASSERT_EQUAL_PTR(&first, &duplicate);
  TEST_ASSERT_EQUAL_UINT16(2, duplicate.value.ref_count);
  TEST_ASSERT_EQUAL_UINT16(1, duplicate.value.reachable_ref_count);
  TEST_ASSERT_EQUAL_UINT16(1, duplicate.child_count);

  auto& fetched_second = tree.get(second.index);
  TEST_ASSERT_EQUAL_PTR(&second, &fetched_second);
}

void test_RefTreeLocalArrayToVectorTransitionKeepsNodes() {
  auto tree = RefTree{};
  for (std::size_t i = 0; i < 33; ++i) {
    auto& node =
        tree.Emplace(MakePointer(i), static_cast<std::uint16_t>(i + 1));
    TEST_ASSERT_EQUAL_UINT16(static_cast<std::uint16_t>(i), node.index);
  }

  for (std::size_t i = 0; i < 33; ++i) {
    auto& node = tree.get(static_cast<RefTree::Index>(i));
    TEST_ASSERT_EQUAL_UINT16(static_cast<std::uint16_t>(i), node.index);
    TEST_ASSERT_EQUAL(static_cast<std::uint64_t>(MakePointer(i)),
                      static_cast<std::uint64_t>(node.value.pointer));
  }

  auto& duplicate = tree.Emplace(MakePointer(5), 99);
  TEST_ASSERT_EQUAL_UINT16(5, duplicate.index);
  auto& unique = tree.Emplace(MakePointer(33), 34);
  TEST_ASSERT_EQUAL_UINT16(33, unique.index);
}

void test_RefTreeForEachTraversesSimpleTreeOnce() {
  auto tree = RefTree{};
  auto& root = tree.Emplace(MakePointer(0), 1);
  auto& child1 = tree.Emplace(MakePointer(1), 1);
  auto& child2 = tree.Emplace(MakePointer(2), 1);
  auto& grandchild = tree.Emplace(MakePointer(3), 1);

  root.PushChild(child1.index);
  root.PushChild(child2.index);
  child1.PushChild(grandchild.index);

  std::vector<RefTree::Index> visited;
  auto result = root.ForEach([&](auto& node) {
    visited.push_back(node.index);
    return true;
  });

  TEST_ASSERT(result);
  TEST_ASSERT_EQUAL_UINT32(3, visited.size());
  TEST_ASSERT(ContainsIndex(visited, child1.index));
  TEST_ASSERT(ContainsIndex(visited, child2.index));
  TEST_ASSERT(ContainsIndex(visited, grandchild.index));
  TEST_ASSERT_FALSE(ContainsIndex(visited, root.index));
}

void test_RefTreeForEachStopsInvokingCallbackAfterFalse() {
  auto tree = RefTree{};
  auto& root = tree.Emplace(MakePointer(0), 1);
  auto& first = tree.Emplace(MakePointer(1), 1);
  auto& later = tree.Emplace(MakePointer(2), 1);
  auto& later_child = tree.Emplace(MakePointer(3), 1);

  root.PushChild(first.index);
  root.PushChild(later.index);
  later.PushChild(later_child.index);

  std::vector<RefTree::Index> visited;
  auto result = root.ForEach([&](auto& node) {
    visited.push_back(node.index);
    return false;
  });

  TEST_ASSERT_FALSE(result);
  TEST_ASSERT_EQUAL_UINT32(1, visited.size());
  TEST_ASSERT_EQUAL_UINT16(first.index, visited[0]);
  TEST_ASSERT_FALSE(ContainsIndex(visited, later_child.index));
}

void test_RefTreeForEachTerminatesWithCycle() {
  auto tree = RefTree{};
  auto& node0 = tree.Emplace(MakePointer(0), 1);
  auto& node1 = tree.Emplace(MakePointer(1), 1);
  auto& node2 = tree.Emplace(MakePointer(2), 1);

  node0.PushChild(node1.index);
  node1.PushChild(node2.index);
  node2.PushChild(node1.index);

  std::vector<RefTree::Index> visited;
  auto result = node0.ForEach([&](auto& node) {
    visited.push_back(node.index);
    return true;
  });

  TEST_ASSERT(result);
  TEST_ASSERT(ContainsIndex(visited, node1.index));
  TEST_ASSERT(ContainsIndex(visited, node2.index));
  TEST_ASSERT_FALSE(ContainsIndex(visited, node0.index));
}

void test_RefTreeForEachSkipsRootInBackEdgeCycle() {
  auto tree = RefTree{};
  auto& root = tree.Emplace(MakePointer(0), 1);
  auto& child = tree.Emplace(MakePointer(1), 1);

  root.PushChild(child.index);
  child.PushChild(root.index);

  std::vector<RefTree::Index> visited;
  auto result = root.ForEach([&](auto& node) {
    visited.push_back(node.index);
    return true;
  });

  TEST_ASSERT(result);
  TEST_ASSERT_EQUAL_UINT32(1, visited.size());
  TEST_ASSERT_EQUAL_UINT16(child.index, visited[0]);
  TEST_ASSERT_FALSE(ContainsIndex(visited, root.index));
}

void test_RefTreeForEachAcceptsMoveOnlyTemporaryCallback() {
  auto tree = RefTree{};
  auto& root = tree.Emplace(MakePointer(0), 1);
  auto& child = tree.Emplace(MakePointer(1), 1);

  root.PushChild(child.index);

  std::vector<RefTree::Index> visited;
  auto result = root.ForEach(MoveOnlyVisitor{visited});

  // cppcheck-suppress-begin *
  // false positive
  TEST_ASSERT(result);
  TEST_ASSERT_EQUAL_UINT32(1, visited.size());
  TEST_ASSERT_EQUAL_UINT16(child.index, visited[0]);
  // cppcheck-suppress-end *
}

void test_RefTreeForEachAcceptsMoveOnlyLValueCallback() {
  auto tree = RefTree{};
  auto& root = tree.Emplace(MakePointer(0), 1);
  auto& child = tree.Emplace(MakePointer(1), 1);

  root.PushChild(child.index);

  std::vector<RefTree::Index> visited;
  auto visitor = MoveOnlyVisitor{visited};
  auto result = root.ForEach(visitor);

  // cppcheck-suppress-begin *
  // false positive
  TEST_ASSERT(result);
  TEST_ASSERT_EQUAL_UINT32(1, visited.size());
  TEST_ASSERT_EQUAL_UINT16(child.index, visited[0]);
  // cppcheck-suppress-end *
}

void test_RefTreeIsReachableHandlesTrueFalseAndCycles() {
  auto tree = RefTree{};
  auto& node0 = tree.Emplace(MakePointer(0), 1);
  auto& node1 = tree.Emplace(MakePointer(1), 1);
  auto& node2 = tree.Emplace(MakePointer(2), 1);
  auto& node3 = tree.Emplace(MakePointer(3), 1);
  auto& node4 = tree.Emplace(MakePointer(4), 1);
  auto& node5 = tree.Emplace(MakePointer(5), 1);

  node0.PushChild(node1.index);
  node0.PushChild(node2.index);
  node1.PushChild(node3.index);
  node3.PushChild(node1.index);
  node5.PushChild(node5.index);

  TEST_ASSERT(node0.IsReachable(node3.index));
  TEST_ASSERT(node3.IsReachable(node1.index));
  TEST_ASSERT_FALSE(node1.IsReachable(node2.index));
  TEST_ASSERT_FALSE(node0.IsReachable(node4.index));
  TEST_ASSERT(node5.IsReachable(node5.index));
  TEST_ASSERT_FALSE(node5.IsReachable(node0.index));
}

void test_RefTreeEdgeTransitionAndOrderPastLocalCapacity() {
  auto tree = RefTree{};
  auto const root_index = tree.Emplace(MakePointer(0), 1).index;
  for (std::size_t i = 1; i <= 70; ++i) {
    auto& child = tree.Emplace(MakePointer(i), 1);
    tree.get(root_index).PushChild(child.index);
  }

  auto& root = tree.get(root_index);
  TEST_ASSERT_EQUAL_UINT16(70, root.child_count);
  std::vector<RefTree::Index> visited;
  auto result = root.ForEach([&](auto& node) {
    visited.push_back(node.index);
    return true;
  });
  TEST_ASSERT(result);
  TEST_ASSERT_EQUAL_UINT32(70, visited.size());
  for (std::size_t i = 1; i <= 70; ++i) {
    TEST_ASSERT_EQUAL_UINT16(static_cast<std::uint16_t>(i), visited[i - 1]);
  }
}

void test_RefTreeDuplicateEdgesArePreserved() {
  auto tree = RefTree{};
  auto& root = tree.Emplace(MakePointer(0), 1);
  auto& child = tree.Emplace(MakePointer(1), 1);

  root.PushChild(child.index);
  root.PushChild(child.index);

  TEST_ASSERT_EQUAL_UINT16(2, root.child_count);
  std::vector<RefTree::Index> visited;
  root.ForEach([&](auto& node) {
    visited.push_back(node.index);
    return true;
  });
  TEST_ASSERT_EQUAL_UINT32(1, visited.size());
  TEST_ASSERT_EQUAL_UINT16(child.index, visited[0]);
}

void test_RefTreeIsReachableInsideForEach() {
  auto tree = RefTree{};
  auto& root = tree.Emplace(MakePointer(0), 1);
  auto& child = tree.Emplace(MakePointer(1), 1);
  auto& grandchild = tree.Emplace(MakePointer(2), 1);

  root.PushChild(child.index);
  child.PushChild(grandchild.index);

  auto result = root.ForEach([&](auto& node) {
    TEST_ASSERT(node.IsReachable(grandchild.index) ||
                (node.index == grandchild.index));
    return true;
  });
  TEST_ASSERT(result);
}
}  // namespace ae::test_ref_tree

int test_ref_tree() {
  UNITY_BEGIN();
  RUN_TEST(
      ae::test_ref_tree::test_RefTreeEmplaceDeduplicatesAndPreservesNodeData);
  RUN_TEST(
      ae::test_ref_tree::test_RefTreeLocalArrayToVectorTransitionKeepsNodes);
  RUN_TEST(ae::test_ref_tree::test_RefTreeForEachTraversesSimpleTreeOnce);
  RUN_TEST(
      ae::test_ref_tree::test_RefTreeForEachStopsInvokingCallbackAfterFalse);
  RUN_TEST(ae::test_ref_tree::test_RefTreeForEachTerminatesWithCycle);
  RUN_TEST(ae::test_ref_tree::test_RefTreeForEachSkipsRootInBackEdgeCycle);
  RUN_TEST(
      ae::test_ref_tree::test_RefTreeForEachAcceptsMoveOnlyTemporaryCallback);
  RUN_TEST(ae::test_ref_tree::test_RefTreeForEachAcceptsMoveOnlyLValueCallback);
  RUN_TEST(ae::test_ref_tree::test_RefTreeIsReachableHandlesTrueFalseAndCycles);
  RUN_TEST(
      ae::test_ref_tree::test_RefTreeEdgeTransitionAndOrderPastLocalCapacity);
  RUN_TEST(ae::test_ref_tree::test_RefTreeDuplicateEdgesArePreserved);
  RUN_TEST(ae::test_ref_tree::test_RefTreeIsReachableInsideForEach);
  return UNITY_END();
}
