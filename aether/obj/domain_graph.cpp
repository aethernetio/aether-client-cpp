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

#include "aether/obj/domain_graph.h"

#include <cassert>

#include "aether/obj/obj.h"

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
}  // namespace ae
