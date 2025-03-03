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

#include "aether/ptr/ptr_management.h"

#include <utility>

namespace ae {

RefTree::RefTree() = default;

RefTree::RefTree(void const* ptr, std::uint8_t ref_count)
    : pointer(ptr), ref_count{ref_count}, reachable_ref_count{} {}

bool RefTree::IsReachable(void const* ptr, std::set<RefTree*>&& visited) {
  visited.insert(this);

  for (auto* ref_tree : children) {
    if (ref_tree->pointer == ptr) {
      return true;
    }
    // prevent cycles
    if (auto it = visited.find(ref_tree); it == std::end(visited)) {
      if (ref_tree->IsReachable(ptr, std::move(visited))) {
        return true;
      }
    }
  }

  return false;
}

}  // namespace ae
