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

#ifndef AETHER_OBJ_DOMAIN_H_
#define AETHER_OBJ_DOMAIN_H_

#include <map>
#include <cstdint>
#include <cassert>

#include "aether/common.h"

#include "aether/ptr/ptr_view.h"

#include "aether/obj/obj_id.h"
#include "aether/obj/registry.h"
#include "aether/obj/obj_mapper.h"
#include "aether/obj/idomain_storage.h"

namespace ae {
class Obj;
class DomainGraph;

class Domain {
  friend class DomainGraph;

 public:
  Domain(TimePoint p, IDomainStorage& storage, IObjMapper& obj_mapper);

  TimePoint Update(TimePoint current_time);

  // Map an class to new id
  ObjId MapObj(ClassIdentity const& class_identity);

  // Search for the object by obj_id.
  Ptr<Obj> Find(ObjId obj_id) const;

  // Add pointer to the object into domain
  void AddObject(ObjId id, Ptr<Obj> const& obj);
  // Remove pointer to the object from domain
  void RemoveObject(Obj* obj);

  // Completely erase object from domain and the storage
  void EraseObject(Obj* obj);

 private:
  Ptr<Obj> ConstructObj(Factory const& factory, ObjId id);

  bool IsLast(uint32_t class_id) const;
  bool IsExisting(uint32_t class_id) const;

  Factory* FindClassFactory(std::uint32_t class_id);
  Factory* GetMostRelatedFactory(ObjId id);

  TimePoint update_time_;
  IDomainStorage* storage_;
  IObjMapper* obj_mapper_;
  Registry* registry_;

  std::map<ObjId::Type, PtrView<Obj>> id_objects_;
};
}  // namespace ae

#endif  // AETHER_OBJ_DOMAIN_H_
