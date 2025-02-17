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

#include "aether/obj/registry.h"

#include <iostream>
#include <utility>
#include <algorithm>

#include "aether/crc.h"
#include "aether/obj/obj_tele.h"

namespace ae {
Registry::Registry()
    : base_to_derived_(GetRelations()), factories_(GetFactories()) {}

void Registry::RegisterClass(uint32_t cls_id, std::uint32_t base_id,
                             Factory&& factory) {
  Factories& factories = GetFactories();
#ifdef DEBUG
  // Fixme: Commented out to fix the build crash in MinGW
  /*std::cout << "Registering class " << factory.class_name << " id " << cls_id
            << " with base " << base_id << std::endl;*/

  // TODO: move into compil-time.
  // TODO: check at compile-time that the base_id to be existing class.
  // Visual Studio 2017 has a bug with multiple static inline members
  // initialization
  // https://developercommunity.visualstudio.com/t/multiple-initializations-of-inline-static-data-mem/261624
  // Refer to
  // https://learn.microsoft.com/en-us/cpp/overview/compiler-versions?view=msvc-170
#  if !defined(_MSC_VER) || _MSC_VER >= 1920
  assert(factories.find(cls_id) == factories.end());
#  endif  // !defined(_MSC_VER) || _MSC_VER >= 1920
#endif    // DEBUG
  factories.emplace(cls_id, std::move(factory));
  // TODO: maybe remove this check
  if (base_id != crc32::from_literal("Obj").value) {
    GetRelations()[base_id].push_back(cls_id);
  }
}

void Registry::UnregisterClass(uint32_t cls_id) {
  if (auto it = factories_.find(cls_id); it != factories_.end()) {
    factories_.erase(it);
  }

  if (auto it = base_to_derived_.find(cls_id); it != base_to_derived_.end()) {
    base_to_derived_.erase(it);
  }

  for (auto it = base_to_derived_.begin(); it != base_to_derived_.end();) {
    it->second.erase(std::remove(it->second.begin(), it->second.end(), cls_id),
                     it->second.end());
    it = it->second.empty() ? base_to_derived_.erase(it) : std::next(it);
  }
}

bool Registry::IsExisting(uint32_t class_id) const {
  return factories_.find(class_id) != factories_.end();
}

int Registry::GenerationDistanceInternal(std::uint32_t base_id,
                                         std::uint32_t derived_id) const {
  auto d = base_to_derived_.find(base_id);
  // The base class is final.
  if (d == base_to_derived_.end()) {
    return -1;
  }

  for (auto& c : d->second) {
    if (derived_id == c) {
      return 1;
    }

    int distance = GenerationDistanceInternal(c, derived_id);
    if (distance >= 0) {
      return distance + 1;
    }
  }

  return -1;
}

int Registry::GenerationDistance(std::uint32_t base_id,
                                 std::uint32_t derived_id) const {
  if (!IsExisting(base_id) || !IsExisting(derived_id)) {
    return -1;
  }

  if (base_id == derived_id) {
    return 0;
  }

  return GenerationDistanceInternal(base_id, derived_id);
}

Factory* Registry::FindFactory(uint32_t class_id) {
  auto it = factories_.find(class_id);
  if (it == factories_.end()) {
    return nullptr;
  }
  return &it->second;
}

void Registry::Log() {
#ifdef DEBUG
  const auto& factories = GetFactories();
  for (const auto& c : factories) {
    AE_TELE_DEBUG(ObjectRegistryLog, "name {}, id {}, base_id {}",
                  c.second.class_name, c.second.cls_id, c.second.base_id);
  }
#endif  // DEBUG
}

}  // namespace ae
