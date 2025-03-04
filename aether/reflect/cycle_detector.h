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

#ifndef AETHER_REFLECT_CYCLE_DETECTOR_H_
#define AETHER_REFLECT_CYCLE_DETECTOR_H_

#include <set>
#include <cstddef>
#include <cstdint>

#include "aether/reflect/type_index.h"

namespace ae::reflect {

struct CycleDetector {
  struct ObjEntry {
    bool operator<(ObjEntry const& rhs) const {
      auto lhs_ptr = reinterpret_cast<std::ptrdiff_t>(ptr);
      auto rhs_ptr = reinterpret_cast<std::ptrdiff_t>(rhs.ptr);
      return (static_cast<std::ptrdiff_t>(type_index) + lhs_ptr) <
             (static_cast<std::ptrdiff_t>(rhs.type_index) + rhs_ptr);
    }

    std::uint32_t type_index;
    void const* ptr;
  };

  std::set<ObjEntry> visited_objects_;

  template <typename T>
  void Add(T const* ptr) {
    visited_objects_.insert(ObjEntry{GetTypeIndex<T>(), ptr});
  }

  template <typename T>
  bool IsVisited(T const* ptr) const {
    auto entry = ObjEntry{GetTypeIndex<T>(), ptr};
    return visited_objects_.find(entry) != visited_objects_.end();
  }
};
}  // namespace ae::reflect

#endif  // AETHER_REFLECT_CYCLE_DETECTOR_H_
