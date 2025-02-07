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

RefTree::RefTree(void const* pointer, std::uint8_t ref_count)
    : pointer(pointer),
      ref_count{ref_count},
      reachable_ref_count{},
      children{std::make_unique<std::set<RefTree*>>()} {}

bool RefTree::IsReachable(void const* p, std::set<RefTree*>&& visited) {
  visited.insert(this);

  for (auto* rt : *children) {
    if (rt->pointer == p) {
      return true;
    }
    // prevent cycles
    if (auto it = visited.find(rt); it == std::end(visited)) {
      if (rt->IsReachable(p, std::move(visited))) {
        return true;
      }
    }
  }

  return false;
}

}  // namespace ae
