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

#include "aether/obj/domain.h"

#include <algorithm>

#include "aether/obj/obj.h"
#include "aether/obj/obj_tele.h"

namespace ae {

Domain::Domain(TimePoint p, IDomainStorage& storage, IObjMapper& obj_mapper)
    : update_time_{p},
      storage_{&storage},
      obj_mapper_{&obj_mapper},
      registry_{&Registry::GetRegistry()} {
  auto objs = storage_->EnumerateObjects();
  std::vector<ObjClassId> objects;
  objects.reserve(objs.size());
  for (auto& obj : objs) {
    objects.emplace_back(ObjClassId{obj.id, std::move(obj.classes)});
  }
  obj_mapper_->ReserveIds(objects);
}

TimePoint Domain::Update(TimePoint current_time) {
  update_time_ = current_time;
  auto next_time = current_time + std::chrono::hours(365);
  for (auto& [_, ptr_view] : id_objects_) {
    auto ptr = ptr_view.Lock();
    if (!ptr) {
      continue;
    }
    // TODO: do not call update for someone who is not want it
    ptr->Update(current_time);
    if (ptr->update_time > current_time) {
      next_time = std::min(next_time, ptr->update_time);
    } else if (ptr->update_time < current_time) {
#ifdef DEBUG
      AE_TELE_ERROR(ObjectDomainUpdatePastTime,
                    "Update returned next time point in the past");
#endif  // DEBUG_
    }
  }
  return next_time;
}

ObjId Domain::MapObj(ClassIdentity const& class_identity) {
  auto id = obj_mapper_->GenerateId(class_identity);
  // TODO: add fallback generation or sane error handling for empty id
  assert(id.has_value());
  return id.value();
}

Ptr<Obj> Domain::ConstructObj(Factory const& factory, ObjId obj_id) {
  Ptr<Obj> o = factory.create();
  AddObject(obj_id, o);
  o->domain = this;
  o->obj_id = obj_id;
  return o;
}

bool Domain::IsLast(uint32_t class_id) const {
  return registry_->relations.find(class_id) == registry_->relations.end();
}

bool Domain::IsExisting(uint32_t class_id) const {
  return registry_->IsExisting(class_id);
}

Ptr<Obj> Domain::Find(ObjId obj_id) const {
  if (auto it = id_objects_.find(obj_id.id()); it != id_objects_.end()) {
    return it->second.Lock();
  }
  return {};
}

void Domain::AddObject(ObjId id, Ptr<Obj> const& obj) {
  id_objects_[id.id()] = obj;
}

void Domain::RemoveObject(Obj* obj) { id_objects_.erase(obj->obj_id.id()); }

void Domain::EraseObject(Obj* obj) {
  obj_mapper_->FreeId(obj->obj_id);
  storage_->Remove(obj->obj_id);
}

Factory* Domain::GetMostRelatedFactory(ObjId id) {
  auto classes = storage_->Enumerate(id);
#if DEBUG
  auto class_names = std::vector<std::string_view>{};
  class_names.reserve(classes.size());
  for (auto cid : classes) {
    class_names.emplace_back(registry_->ClassName(cid));
  }

  AE_TELED_DEBUG("For obj {} enumerated classes [{}]", id.id(), class_names);
#else
  AE_TELED_DEBUG("For obj {} enumerated classes [{}]", id.id(), classes);
#endif

  // Remove all unsupported classes.
  classes.erase(
      std::remove_if(std::begin(classes), std::end(classes),
                     [this](auto const& c) { return !IsExisting(c); }),
      std::end(classes));

  if (classes.empty()) {
    return nullptr;
  }

  // Build inheritance chain.
  // from base to derived.
  std::sort(std::begin(classes), std::end(classes),
            [this](auto left, auto right) {
              if (registry_->GenerationDistance(right, left) > 0) {
                return false;
              }
              if (registry_->GenerationDistance(left, right) >= 0) {
                return true;
              }
              // All classes must be in one inheritance chain.
              assert(false);
              return false;
            });

  // Find the Final class for the most derived class provided and create it.
  for (auto& f : registry_->factories) {
    if (IsLast(f.first)) {
      // check with most derived class
      int distance = registry_->GenerationDistance(classes.back(), f.first);
      if (distance >= 0) {
        return &f.second;
      }
    }
  }

  return nullptr;
}

Factory* Domain::FindClassFactory(std::uint32_t class_id) {
  return registry_->FindFactory(class_id);
}
}  // namespace ae
