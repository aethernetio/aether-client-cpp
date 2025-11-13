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

#include <cstddef>
#include <cassert>
#include <algorithm>

namespace ae {

// rvalue std::set<std::size_t>&& is used here to avoid copies and has interface
// with default value. Nobody except first function call owns the visited set.
bool RefTree::Node::IsReachable(Index i,
                                std::set<Index>&& visited) const noexcept {
  visited.insert(index);
  if (index == i) {
    return true;
  }

  bool res = false;
  for (auto const& child_index : children_indices) {
    if (auto it = visited.find(child_index); it != visited.end()) {
      // already visited
      continue;
    }
    res = res || tree->get(child_index).IsReachable(i, std::move(visited));
  }

  return res;
}

RefTree::Node& RefTree::Emplace(Value value) {
  auto it = std::find_if(
      std::begin(nodes_), std::end(nodes_),
      [&](Node const& n) { return n.value.pointer == value.pointer; });
  if (it != std::end(nodes_)) {
    return *it;
  }

  return nodes_.emplace_back(Node{
      value,
      static_cast<Index>(nodes_.size()),
      {},
      this,
  });
}

RefTree::Node& RefTree::get(Index index) {
  assert((static_cast<std::size_t>(index) < nodes_.size()) &&
         "Index out of bounds");
  auto& node = nodes_[static_cast<std::size_t>(index)];
  node.tree = this;
  return node;
}
}  // namespace ae
