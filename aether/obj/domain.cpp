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

DomainGraph::DomainGraph(Domain* domain) : domain(domain) { assert(domain); }

Ptr<Obj> DomainGraph::LoadRootImpl(ObjId obj_id) {
  if (!obj_id.IsValid()) {
    return {};
  }
  // if already loaded
  if (auto obj = domain->Find(obj_id); obj) {
    return obj;
  }

  auto* factory = domain->GetMostRelatedFactory(obj_id);
  if (factory == nullptr) {
    return {};
  }

  auto ptr = domain->ConstructObj(*factory, obj_id);
  factory->load(this, ptr, obj_id);
  return ptr;
}

Ptr<Obj> DomainGraph::LoadCopyImpl(ObjId ref_id, ObjId copy_id) {
  if (!ref_id.IsValid() || !copy_id.IsValid()) {
    return {};
  }
  // if already loaded
  if (auto obj = domain->Find(copy_id); obj) {
    return obj;
  }

  auto* factory = domain->GetMostRelatedFactory(ref_id);
  if (factory == nullptr) {
    assert(false);
    return {};
  }

  auto ptr = domain->ConstructObj(*factory, copy_id);
  // load new object with ref_id
  factory->load(this, ptr, ref_id);
  return ptr;
}

void DomainGraph::SaveRootImpl(Ptr<Obj> const& ptr, ObjId obj_id) {
  if (!ptr) {
    return;
  }
  if (auto* factory = domain->FindClassFactory(ptr->GetClassId());
      factory != nullptr) {
    factory->save(this, ptr, obj_id);
  }
}

std::unique_ptr<IDomainStorageReader> DomainGraph::GetReader(
    DomainQuery const& query) {
  auto load = domain->storage_->Load(query);
  if ((load.result == DomainLoadResult::kEmpty) ||
      (load.result == DomainLoadResult::kRemoved)) {
    load.reader = std::make_unique<DomainStorageReaderEmpty>();
  }

  assert(load.reader && "Reader must be created!");
  return std::move(load.reader);
}

std::unique_ptr<IDomainStorageWriter> DomainGraph::GetWriter(
    DomainQuery const& query) {
  auto writer = domain->storage_->Store(query);
  assert(writer && "Writer must be created!");
  return writer;
}

Domain::Domain(TimePoint p, IDomainStorage& storage)
    : update_time_{p},
      storage_(&storage),
      registry_{&Registry::GetRegistry()} {}

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

void Domain::RemoveObject(Obj* ptr) { id_objects_.erase(ptr->obj_id.id()); }

Factory* Domain::GetMostRelatedFactory(ObjId id) {
  auto classes = storage_->Enumerate(id);

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
