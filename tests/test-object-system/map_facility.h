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

#ifndef TESTS_TEST_OBJECT_SYSTEM_MAP_FACILITY_H_
#define TESTS_TEST_OBJECT_SYSTEM_MAP_FACILITY_H_

#include <unordered_map>
#include <vector>
#include <cstdint>

#include "aether/obj/domain.h"
#include "aether/obj/obj_id.h"

namespace ae {
class MapFacility : public IDomainFacility {
 public:
  using ObjKey = ObjId::Type;
  using Data = std::vector<std::uint8_t>;
  using VersionData = std::unordered_map<std::uint8_t, Data>;
  using ClassData = std::unordered_map<std::uint32_t, VersionData>;
  using ObjClassData = std::unordered_map<ObjKey, ClassData>;

  ObjClassData map_;

  void Store(const ObjId& obj_id, std::uint32_t class_id, std::uint8_t version,
             const std::vector<uint8_t>& os) override {
    map_[obj_id.id()][class_id][version] = os;
  }

  std::vector<std::uint32_t> Enumerate(const ObjId& obj_id) override {
    auto& class_data = map_[obj_id.id()];
    std::vector<std::uint32_t> classes;
    for (auto& [cls, _] : class_data) {
      classes.push_back(cls);
    }
    return classes;
  }

  void Load(const ObjId& obj_id, std::uint32_t class_id, std::uint8_t version,
            std::vector<uint8_t>& is) override {
    is = map_[obj_id.id()][class_id][version];
  }

  void Remove(const ObjId& obj_id) override { map_.erase(obj_id.id()); }

#if defined AE_DISTILLATION
  void CleanUp() override { map_.clear(); }
#endif
};
}  // namespace ae

#endif  // TESTS_TEST_OBJECT_SYSTEM_MAP_FACILITY_H_ */
