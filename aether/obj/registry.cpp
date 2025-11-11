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

#include <utility>

#include "aether/crc.h"
#include "aether/obj/obj_tele.h"

namespace ae {
Registry& Registry::GetRegistry() {
  static Registry registry;
  return registry;
}

void Registry::RegisterClass(uint32_t cls_id, std::uint32_t base_id,
                             Factory&& factory) {
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
  auto no_duplication = factories.find(cls_id) == factories.end();
  assert(no_duplication && "Duplicate class id in registry");
#  endif  // !defined(_MSC_VER) || _MSC_VER >= 1920
#endif    // DEBUG
  factories.emplace(cls_id, std::move(factory));
  // TODO: maybe remove this check
  if (base_id != crc32::from_literal("Obj").value) {
    relations[base_id].push_back(cls_id);
  }
}

bool Registry::IsExisting(uint32_t class_id) {
  return factories.find(class_id) != factories.end();
}

int Registry::GenerationDistanceInternal(std::uint32_t base_id,
                                         std::uint32_t derived_id) {
  auto d = relations.find(base_id);
  // The base class is final.
  if (d == relations.end()) {
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
                                 std::uint32_t derived_id) {
  if (!IsExisting(base_id) || !IsExisting(derived_id)) {
    return -1;
  }

  if (base_id == derived_id) {
    return 0;
  }

  return GenerationDistanceInternal(base_id, derived_id);
}

Factory* Registry::FindFactory(std::uint32_t base_id) {
  auto it = factories.find(base_id);
  if (it == factories.end()) {
    return nullptr;
  }
  return &it->second;
}

void Registry::Log() {
#ifdef DEBUG
  for (const auto& c : factories) {
    AE_TELE_DEBUG(ObjectRegistryLog, "name {}, id {}, base_id {}",
                  c.second.class_name, c.second.cls_id, c.second.base_id);
  }
#endif  // DEBUG
}

}  // namespace ae
