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

#ifndef AETHER_PTR_REF_TREE_H_
#define AETHER_PTR_REF_TREE_H_

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ae {

class RefTree;

namespace ref_tree_internal {

using Index = std::uint16_t;
using EdgeIndex = std::uint16_t;
inline constexpr EdgeIndex kInvalidEdgeIndex{
    std::numeric_limits<EdgeIndex>::max()};
inline constexpr std::size_t kRefTreeLocalNodeCapacity{32};
inline constexpr std::size_t kRefTreeLocalEdgeCapacity{32};

struct Value {
  std::uintptr_t pointer{};
  std::uint16_t ref_count{};
  std::uint16_t reachable_ref_count{};
};

struct Edge {
  Index child_index{};
  EdgeIndex next_edge_index{kInvalidEdgeIndex};
};

class Node {
 public:
  void PushChild(Index i);

  template <typename TFunc>
  bool ForEach(TFunc& func);

  template <typename TFunc>
    requires(!std::is_lvalue_reference_v<TFunc>)
  bool ForEach(TFunc&& func);
  bool IsReachable(Index i) const;

  Value value{};
  Index index{};
  std::uint16_t child_count{};
  bool children_expanded{false};

 private:
  // Outgoing edges are stored in RefTree's centralized linked list; tail keeps
  // append order stable.
  EdgeIndex first_edge_index_{kInvalidEdgeIndex};
  EdgeIndex last_edge_index_{kInvalidEdgeIndex};
  ae::RefTree* tree_{};
  mutable std::size_t for_each_visit_index_{0};
  mutable std::size_t reachable_visit_index_{0};

  template <typename TFunc>
  bool ForEachImpl(TFunc& func);

  friend class ae::RefTree;
};

template <typename T, std::size_t kLocalCapacity>
class Storage {
 public:
  std::size_t Size() const noexcept;
  T& At(std::size_t index) noexcept;
  T const& At(std::size_t index) const noexcept;
  T& PushBack(T const& value);

 private:
  struct LocalStorage {
    std::array<T, kLocalCapacity> values{};
    std::size_t size{};
  };
  using DynamicStorage = std::vector<T>;

  void MoveLocalToDynamic();

  std::variant<LocalStorage, DynamicStorage> storage_{LocalStorage{}};
};

using NodeStorage = Storage<Node, kRefTreeLocalNodeCapacity>;
using EdgeStorage = Storage<Edge, kRefTreeLocalEdgeCapacity>;

}  // namespace ref_tree_internal

class RefTree {
 public:
  ~RefTree() = default;
  RefTree() = default;
  RefTree(RefTree const&) = delete;
  RefTree& operator=(RefTree const&) = delete;
  RefTree(RefTree&& other) noexcept = delete;
  RefTree& operator=(RefTree&& other) noexcept = delete;

  using Index = ref_tree_internal::Index;
  using Value = ref_tree_internal::Value;
  using Node = ref_tree_internal::Node;

  friend class ref_tree_internal::Node;

  Node& Emplace(std::uintptr_t pointer, std::uint16_t ref_count);
  Node& get(Index index);

 private:
  using Edge = ref_tree_internal::Edge;
  using EdgeIndex = ref_tree_internal::EdgeIndex;
  static constexpr EdgeIndex kInvalidEdgeIndex =
      ref_tree_internal::kInvalidEdgeIndex;

  EdgeIndex EmplaceEdge(Index child_index);
  Edge& edge(EdgeIndex index);
  Edge const& edge(EdgeIndex index) const;
  std::size_t NextForEachVisitIndex() noexcept;
  std::size_t NextReachableVisitIndex() noexcept;
  template <typename TFunc>
  bool ForEachChildren(Node& parent, TFunc& func, std::size_t visit_index);
  bool IsReachableFrom(Node const& node, Index target, std::size_t visit_index);
  Node* FindNode(std::uintptr_t pointer) noexcept;

  ref_tree_internal::NodeStorage nodes_{};
  ref_tree_internal::EdgeStorage edges_{};
  std::size_t next_for_each_visit_index_{1};
  std::size_t next_reachable_visit_index_{1};
};

template <typename TFunc>
bool ref_tree_internal::Node::ForEach(TFunc& func) {
  assert((tree_ != nullptr) && "Node tree is null");
  return ForEachImpl(func);
}

template <typename TFunc>
  requires(!std::is_lvalue_reference_v<TFunc>)
bool ref_tree_internal::Node::ForEach(TFunc&& func) {
  auto callback = std::forward<TFunc>(func);
  return ForEachImpl(callback);
}

template <typename TFunc>
bool ref_tree_internal::Node::ForEachImpl(TFunc& func) {
  assert((tree_ != nullptr) && "Node tree is null");
  auto const visit_index = tree_->NextForEachVisitIndex();
  for_each_visit_index_ = visit_index;
  return tree_->ForEachChildren(*this, func, visit_index);
}

template <typename TFunc>
bool RefTree::ForEachChildren(Node& parent, TFunc& func,
                              std::size_t visit_index) {
  auto edge_index = parent.first_edge_index_;
  std::vector<EdgeIndex> stack{};
  while (edge_index != kInvalidEdgeIndex || !stack.empty()) {
    if (edge_index == kInvalidEdgeIndex) {
      edge_index = stack.back();
      stack.pop_back();
      continue;
    }
    auto const current_edge = edge(edge_index);
    auto const child_index = current_edge.child_index;
    auto const next_edge_index = current_edge.next_edge_index;
    edge_index = next_edge_index;
    auto& child = get(child_index);
    if (child.for_each_visit_index_ == visit_index) {
      continue;
    }
    child.for_each_visit_index_ = visit_index;
    if (!func(child)) {
      return false;
    }
    if (child.first_edge_index_ != kInvalidEdgeIndex) {
      if (edge_index != kInvalidEdgeIndex) {
        stack.push_back(edge_index);
      }
      edge_index = child.first_edge_index_;
    }
  }
  return true;
}

template <typename T, std::size_t kLocalCapacity>
std::size_t ref_tree_internal::Storage<T, kLocalCapacity>::Size()
    const noexcept {
  if (auto const* dynamic = std::get_if<DynamicStorage>(&storage_);
      dynamic != nullptr) {
    return dynamic->size();
  }
  return std::get<LocalStorage>(storage_).size;
}

template <typename T, std::size_t kLocalCapacity>
T& ref_tree_internal::Storage<T, kLocalCapacity>::At(
    std::size_t index) noexcept {
  assert((index < Size()) && "Storage index out of range");
  if (auto* dynamic = std::get_if<DynamicStorage>(&storage_);
      dynamic != nullptr) {
    return (*dynamic)[index];
  }
  return std::get<LocalStorage>(storage_).values[index];
}

template <typename T, std::size_t kLocalCapacity>
T const& ref_tree_internal::Storage<T, kLocalCapacity>::At(
    std::size_t index) const noexcept {
  assert((index < Size()) && "Storage index out of range");
  if (auto const* dynamic = std::get_if<DynamicStorage>(&storage_);
      dynamic != nullptr) {
    return (*dynamic)[index];
  }
  return std::get<LocalStorage>(storage_).values[index];
}

template <typename T, std::size_t kLocalCapacity>
void ref_tree_internal::Storage<T, kLocalCapacity>::MoveLocalToDynamic() {
  auto& local = std::get<LocalStorage>(storage_);
  auto dynamic =
      DynamicStorage(local.values.begin(), local.values.begin() + local.size);
  storage_.template emplace<DynamicStorage>(std::move(dynamic));
}

template <typename T, std::size_t kLocalCapacity>
T& ref_tree_internal::Storage<T, kLocalCapacity>::PushBack(T const& value) {
  if (auto* dynamic = std::get_if<DynamicStorage>(&storage_);
      dynamic != nullptr) {
    dynamic->push_back(value);
    return dynamic->back();
  }
  auto& local = std::get<LocalStorage>(storage_);
  if (local.size < kLocalCapacity) {
    local.values[local.size] = value;
    ++local.size;
    return local.values[local.size - 1];
  }
  MoveLocalToDynamic();
  auto* dynamic = std::get_if<DynamicStorage>(&storage_);
  assert((dynamic != nullptr) && "Dynamic storage missing after move");
  dynamic->push_back(value);
  return dynamic->back();
}

}  // namespace ae

#endif  // AETHER_PTR_REF_TREE_H_
