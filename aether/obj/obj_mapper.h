/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_OBJ_OBJ_MAPPER_H_
#define AETHER_OBJ_OBJ_MAPPER_H_

#include <cstdint>
#include <optional>

#include "aether/obj/obj_id.h"

namespace ae {
// a list of class ids related to each other in inheritance hierarchy
struct ClassIdentity : std::vector<std::uint32_t> {};
struct ObjClassId {
  ObjId id;
  ClassIdentity class_id;
};
class IObjMapper {
 public:
  virtual ~IObjMapper() = default;

  /**
   * \brief Generate a new unique obj id for the given class relations.
   */
  virtual std::optional<ObjId> GenerateId(ClassIdentity const& class_id) = 0;
  /**
   * \brief Free the given obj id.
   */
  virtual void FreeId(ObjId id) = 0;
  /**
   * \brief Reserve ids for a list of objects.
   */
  virtual void ReserveIds(std::vector<ObjClassId> const& obj_class_ids) = 0;
};
}  // namespace ae

#endif  // AETHER_OBJ_OBJ_MAPPER_H_
