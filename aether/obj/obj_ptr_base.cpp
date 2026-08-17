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

#include "aether/obj/obj_ptr_base.h"

#include "aether/obj/domain.h"
#include "aether/obj/obj_id.h"

namespace ae {

ObjectPtrBase::ObjectPtrBase()
    : domain_{nullptr}, id_{}, flags_{ObjFlags::kUnloaded} {}

ObjectPtrBase::ObjectPtrBase(Domain* domain, ObjId obj_id, ObjFlags flags)
    : domain_{domain}, id_{obj_id}, flags_{flags} {}

ObjectPtrBase::ObjectPtrBase(ObjectPtrBase const& ptr) noexcept = default;

ObjId ObjectPtrBase::id() const { return id_; }
ObjFlags ObjectPtrBase::flags() const { return flags_; }
Domain* ObjectPtrBase::domain() const { return domain_; }

void ObjectPtrBase::SetFlags(ObjFlags flags) { flags_ = flags; }

ObjectPtrBase& ObjectPtrBase::operator=(ObjectPtrBase const& ptr) noexcept =
    default;

namespace seri {
using ObjPtrBaseSerializer =
    Serializer<BinaryArchive<DomainBuffer>, ObjectPtrBase>;

SeriResult ObjPtrBaseSerializer::Seri(Archive& archive,
                                      Meta<ObjectPtrBase const> meta) const {
  TRY_RESULT((archive.buffer().Write(DataTag{meta.value.id_})));
  return archive.buffer().Write(DataTag{meta.value.flags_});
}

SeriResult ObjPtrBaseSerializer::Deseri(Archive& archive,
                                        Meta<ObjectPtrBase> meta) const {
  meta.value.domain_ = archive.buffer().domain_graph->domain;
  TRY_RESULT((archive.buffer().Read(DataTag{meta.value.id_})));
  return archive.buffer().Read(DataTag{meta.value.flags_});
}
}  // namespace seri
}  // namespace ae
