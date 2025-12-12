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

#include <set>
#include <vector>
#include <cstdint>
#include <utility>

namespace ae {

class RefTree {
 public:
  using Index = std::uint8_t;

  struct Value {
    std::uintptr_t pointer;
    std::uint8_t ref_count;
    std::uint8_t reachable_ref_count;
  };

  struct Node {
    // apply func to each child node
    template <typename TFunc>
    bool ForEach(TFunc&& func, std::set<Index>&& visited = {}) noexcept {
      visited.insert(index);
      for (auto const& child_index : children_indices) {
        if (auto next = func(tree->get(child_index)); !next) {
          break;
        }
      }

      for (auto const& child_index : children_indices) {
        if (auto it = visited.find(child_index); it != visited.end()) {
          // already visited
          continue;
        }
        if (auto next =
                tree->get(child_index)
                    .ForEach(std::forward<TFunc>(func), std::move(visited));
            !next) {
          return false;
        }
      }
      return true;
    }

    // Check if the index is reachable from the node
    bool IsReachable(Index i, std::set<Index>&& visited = {}) const noexcept;

    Value value;
    Index index;
    std::vector<Index> children_indices;
    RefTree* tree;
  };

  Node& Emplace(std::uintptr_t pointer, std::uint8_t ref_count);
  Node& get(Index index);

 private:
  // list of nodes, head is always 0
  std::vector<Node> nodes_;
};
}  // namespace ae

#endif  // AETHER_PTR_REF_TREE_H_
