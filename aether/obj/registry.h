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

#ifndef AETHER_OBJ_REGISTRY_H_
#define AETHER_OBJ_REGISTRY_H_

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>

#include "aether/config.h"
#include "aether/obj/obj_ptr.h"
#include "aether/events/delegate.h"

namespace ae {
class Obj;
class Domain;

struct Factory {
  Delegate<ObjPtr<Obj>()> create;
  Delegate<ObjPtr<Obj>(Domain* domain, ObjPtr<Obj> prefab)> load;
  Delegate<void(Domain* domain, ObjPtr<Obj> const& obj)> save;
#ifdef DEBUG
  std::string class_name{};
  std::uint32_t cls_id{};
  std::uint32_t base_id{};
#endif  // DEBUG
};

class Registry {
 public:
  using Relations = std::unordered_map<uint32_t, std::vector<uint32_t>>;
  using Factories = std::unordered_map<uint32_t, Factory>;

  Relations base_to_derived_;
  Factories factories_;

  static void RegisterClass(uint32_t cls_id, std::uint32_t base_id,
                            Factory&& factory);
  static void Log();

  static Relations& GetRelations() {
    static Relations relations;
    return relations;
  }
  static Factories& GetFactories() {
    static Factories factories;
    return factories;
  }

  Registry();

  void UnregisterClass(uint32_t cls_id);

  bool IsExisting(uint32_t class_id) const;

  int GenerationDistanceInternal(std::uint32_t base_id,
                                 std::uint32_t derived_id) const;

  // Calculates distance from base to derived in generations:
  //  -1 - derived is not inherited directly or indirectly from base or any
  //  class doesn't exist.
  int GenerationDistance(std::uint32_t base_id, std::uint32_t derived_id) const;

  Factory* FindFactory(uint32_t base_id);
};
}  // namespace ae

#endif  // AETHER_OBJ_REGISTRY_H_ */
