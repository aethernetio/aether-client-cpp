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

inline std::size_t GetIndexFromTypeIdAndAddress(void const* obj,
                                                std::uint32_t type_index) {
  std::array<std::uint8_t, sizeof(std::uintptr_t) + sizeof(type_index)> buffer;
  // Concatenate the object's address and type index into one buffer
  *reinterpret_cast<std::uintptr_t*>(buffer.data()) =
      reinterpret_cast<std::uintptr_t>(obj);
  *reinterpret_cast<std::uint32_t*>(buffer.data() + sizeof(std::uintptr_t)) =
      type_index;
  // get the crc32 hash of the buffer
  return crc32::from_buffer(buffer.data(), buffer.size()).value;
}

template <typename T, typename Enable = void>
struct ObjectIndex {
  static constexpr auto TypeIndex = GetTypeIndex<T>();

  static std::size_t GetIndex(T const* obj) {
    return GetIndexFromTypeIdAndAddress(obj, TypeIndex);
  }
};

struct CycleDetector {
  /**
   * \brief Add an object to the cycle detector. If the object is already in the
   * cycle detector, return false. Otherwise, add the object and return true.
   */
  template <typename T>
  bool Add(T const* ptr) {
    auto entry = ObjectIndex<T>::GetIndex(ptr);
    auto [it, ok] = visited_objects.insert(entry);
    return ok;
  }

  std::set<std::size_t> visited_objects;
};
}  // namespace ae::reflect

#endif  // AETHER_REFLECT_CYCLE_DETECTOR_H_
