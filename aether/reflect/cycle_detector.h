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
#include <array>
#include <cstddef>
#include <cstdint>

#include "aether/crc.h"
#include "aether/reflect/type_index.h"

namespace ae::reflect {
template <typename T, typename Enable = void>
struct ObjectIndex {
  static std::size_t GetIndex(T const* obj) {
    std::array<std::uint8_t, sizeof(std::uintptr_t) + sizeof(std::uint32_t)>
        index_data;
    *reinterpret_cast<std::uintptr_t*>(index_data.data()) =
        reinterpret_cast<std::uintptr_t>(obj);
    *reinterpret_cast<std::uint32_t*>(
        index_data.data() + sizeof(std::uintptr_t)) = GetTypeIndex<T>();

    return crc32::from_buffer(index_data.data(), index_data.size()).value;
  }
};

struct CycleDetector {
  template <typename T>
  void Add(T const* ptr) {
    visited_objects.insert(ObjectIndex<T>::GetIndex(ptr));
  }

  template <typename T>
  bool IsVisited(T const* ptr) const {
    auto entry = ObjectIndex<T>::GetIndex(ptr);
    return visited_objects.find(entry) != visited_objects.end();
  }

  std::set<std::size_t> visited_objects;
};
}  // namespace ae::reflect

#endif  // AETHER_REFLECT_CYCLE_DETECTOR_H_
