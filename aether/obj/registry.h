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
#include "aether/ptr/ptr.h"
#include "aether/obj/obj_id.h"

namespace ae {
class Obj;
class Domain;
class DomainGraph;

struct Factory {
  using CreateFunc = Ptr<Obj> (*)();
  using LoadFunc = void (*)(DomainGraph* domain_graph, Ptr<Obj>& obj, ObjId id);
  using SaveFunc = void (*)(DomainGraph* domain_graph, Ptr<Obj> const& obj,
                            ObjId id);

  CreateFunc create;
  LoadFunc load;
  SaveFunc save;
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

  static Registry& GetRegistry();

  void RegisterClass(uint32_t cls_id, std::uint32_t base_id, Factory&& factory);
  void Log();
  bool IsExisting(uint32_t class_id);

  int GenerationDistanceInternal(std::uint32_t base_id,
                                 std::uint32_t derived_id);

  // Calculates distance from base to derived in generations:
  //  -1 - derived is not inherited directly or indirectly from base or any
  //  class doesn't exist.
  int GenerationDistance(std::uint32_t base_id, std::uint32_t derived_id);

  Factory* FindFactory(uint32_t base_id);

  Relations relations;
  Factories factories;
};
}  // namespace ae

#endif  // AETHER_OBJ_REGISTRY_H_ */
