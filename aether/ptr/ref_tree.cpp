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

#include "aether/ptr/ref_tree.h"

#include <cassert>

namespace ae {

void ref_tree_internal::Node::PushChild(Index i) {
  assert((tree_ != nullptr) && "Node tree is null");
  assert((i < tree_->nodes_.Size()) && "Child index is out of range");
  auto const edge_index = tree_->EmplaceEdge(i);
  assert((child_count < std::numeric_limits<std::uint16_t>::max()) &&
         "Child count overflow");
  if (child_count == 0) {
    first_edge_index_ = edge_index;
  } else {
    tree_->edge(last_edge_index_).next_edge_index = edge_index;
  }
  last_edge_index_ = edge_index;
  ++child_count;
}

bool ref_tree_internal::Node::IsReachable(Index i) const {
  assert((tree_ != nullptr) && "Node tree is null");
  auto const visit_index = tree_->NextReachableVisitIndex();
  return tree_->IsReachableFrom(*this, i, visit_index);
}

RefTree::Node& RefTree::Emplace(std::uintptr_t pointer,
                                std::uint16_t ref_count) {
  if (auto* node = FindNode(pointer); node != nullptr) {
    node->tree_ = this;
    return *node;
  }
  auto const size = nodes_.Size();
  assert((size <= std::numeric_limits<Index>::max()) && "Node index overflow");
  auto node = Node{};
  node.value = Value{pointer, ref_count, 0};
  node.index = static_cast<Index>(size);
  node.tree_ = this;
  auto& inserted = nodes_.PushBack(node);
  inserted.tree_ = this;
  return inserted;
}

RefTree::EdgeIndex RefTree::EmplaceEdge(Index child_index) {
  auto const size = edges_.Size();
  assert((size < kInvalidEdgeIndex) && "Edge index overflow");
  edges_.PushBack(Edge{child_index, kInvalidEdgeIndex});
  return static_cast<EdgeIndex>(size);
}

RefTree::Edge& RefTree::edge(EdgeIndex index) {
  assert((index != kInvalidEdgeIndex) && "Invalid edge index");
  return edges_.At(static_cast<std::size_t>(index));
}

RefTree::Edge const& RefTree::edge(EdgeIndex index) const {
  assert((index != kInvalidEdgeIndex) && "Invalid edge index");
  return edges_.At(static_cast<std::size_t>(index));
}

std::size_t RefTree::NextForEachVisitIndex() noexcept {
  return next_for_each_visit_index_++;
}

std::size_t RefTree::NextReachableVisitIndex() noexcept {
  return next_reachable_visit_index_++;
}

bool RefTree::IsReachableFrom(RefTree::Node const& node, Index target,
                              std::size_t visit_index) {
  if (node.reachable_visit_index_ == visit_index) {
    return false;
  }
  node.reachable_visit_index_ = visit_index;
  if (node.index == target) {
    return true;
  }
  auto edge_index = node.first_edge_index_;
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
    if (child_index == target) {
      return true;
    }
    auto& child = get(child_index);
    if (child.reachable_visit_index_ == visit_index) {
      continue;
    }
    child.reachable_visit_index_ = visit_index;
    if (child.index == target) {
      return true;
    }
    if (child.first_edge_index_ != kInvalidEdgeIndex) {
      if (edge_index != kInvalidEdgeIndex) {
        stack.push_back(edge_index);
      }
      edge_index = child.first_edge_index_;
    }
  }
  return false;
}

RefTree::Node& RefTree::get(Index index) {
  auto& node = nodes_.At(static_cast<std::size_t>(index));
  node.tree_ = this;
  return node;
}

RefTree::Node* RefTree::FindNode(std::uintptr_t pointer) noexcept {
  for (std::size_t i = 0; i < nodes_.Size(); ++i) {
    auto& node = nodes_.At(i);
    if (node.value.pointer == pointer) {
      return &node;
    }
  }
  return nullptr;
}
}  // namespace ae
