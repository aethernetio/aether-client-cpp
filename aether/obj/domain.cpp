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
#include "aether/ae_exp_save_stats.h"

namespace ae {

DomainGraph::DomainGraph(Domain* domain) : domain(domain) { assert(domain); }

Ptr<Obj> DomainGraph::LoadRoot(ObjId obj_id) {
  if (!obj_id.is_valid()) {
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
  if (!ref_id.is_valid() || !copy_id.is_valid()) {
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

void DomainGraph::SaveRoot(Ptr<Obj> const& ptr, ObjId obj_id) {
  if (!ptr) {
    return;
  }
  AeExpSaveInc(&AeExpSaveSample::save_root_calls);
  domain->storage_->BeginSaveTransaction();
  if (auto* factory = domain->FindClassFactory(ptr->GetClassId());
      factory != nullptr) {
    factory->save(this, ptr, obj_id);
  }
  domain->storage_->EndSaveTransaction();
}

DomainLoad DomainGraph::GetReader(DomainQuery const& query) {
  return domain->storage_->Load(query);
}

std::unique_ptr<IDomainStorageWriter> DomainGraph::GetWriter(
    DomainQuery const& query) {
  auto writer = domain->storage_->Store(query);
  assert(writer && "Writer must be created!");
  AeExpSaveInc(&AeExpSaveSample::writers_created);
  return writer;
}

Domain::Domain(TimePoint p, IDomainStorage& storage)
    : update_time_{p},
      storage_(&storage),
      registry_{&Registry::GetRegistry()} {}

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

namespace seri {
SeriResult Serializer<BinaryArchive<DomainBuffer>, std::string>::Deseri(
    BinaryArchive<DomainBuffer>& archive, Meta<std::string> val) const {
  std::size_t size{};
  TRY_RESULT(archive.buffer().Read(SizeTag{size}));
  if (size > archive.max_container_load_size()) {
    return Error{container_too_large};
  }
  val.value.resize(size);
  auto* data = reinterpret_cast<std::uint8_t*>(val.value.data());
  return archive.buffer().Read(DataReadTag{data, size});
}

SeriResult Serializer<BinaryArchive<DomainBuffer>, std::string>::Seri(
    BinaryArchive<DomainBuffer>& archive, Meta<std::string const> val) const {
  auto const size = val.value.size();
  TRY_RESULT(archive.buffer().Write(SizeTag{size}));
  auto const* data = reinterpret_cast<std::uint8_t const*>(val.value.data());
  TRY_RESULT(archive.buffer().Write(DataWriteTag{data, size}));
  return Ok{good};
}

SeriResult
Serializer<BinaryArchive<DomainBuffer>, std::vector<std::uint8_t>>::Deseri(
    BinaryArchive<DomainBuffer>& archive,
    Meta<std::vector<std::uint8_t>> val) const {
  std::size_t size{};
  TRY_RESULT(archive.buffer().Read(SizeTag{size}));
  if (size > archive.max_container_load_size()) {
    return Error{container_too_large};
  }
  val.value.resize(size);
  auto* data = reinterpret_cast<std::uint8_t*>(val.value.data());
  return archive.buffer().Read(DataReadTag{data, size});
}

SeriResult
Serializer<BinaryArchive<DomainBuffer>, std::vector<std::uint8_t>>::Seri(
    BinaryArchive<DomainBuffer>& archive,
    Meta<std::vector<std::uint8_t> const> val) const {
  auto const size = val.value.size();
  TRY_RESULT(archive.buffer().Write(SizeTag{size}));
  auto const* data = reinterpret_cast<std::uint8_t const*>(val.value.data());
  TRY_RESULT(archive.buffer().Write(DataWriteTag{data, size}));
  return Ok{good};
}
}  // namespace seri
}  // namespace ae
