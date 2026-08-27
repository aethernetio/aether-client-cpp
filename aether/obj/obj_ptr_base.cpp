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
#include "aether/obj/obj.h"
#include "aether/obj/obj_id.h"
#include "aether/ae_exp_diag.h"

namespace ae {

ObjectPtrBase::ObjectPtrBase()
    : domain_{nullptr}, flags_{ObjFlags::kUnloaded} {}

ObjectPtrBase::ObjectPtrBase(Domain* domain, ObjId obj_id, ObjFlags flags)
    : domain_{domain}, id_{obj_id}, flags_{flags} {}

ObjectPtrBase::ObjectPtrBase(Domain* domain, ObjId obj_id, ObjFlags flags,
                             Ptr<Obj> ptr) noexcept
    : domain_{domain}, id_{obj_id}, flags_{flags}, cached_{std::move(ptr)} {}

ObjectPtrBase::ObjectPtrBase(ObjectPtrBase const& ptr) noexcept
    : domain_{ptr.domain_}, id_{ptr.id_}, flags_{ptr.flags_} {
  if (ptr.cached_) {
    cached_ = ptr.cached_;
  }
}

ObjectPtrBase::ObjectPtrBase(ObjectPtrBase&& ptr) noexcept
    : domain_{ptr.domain_}, id_{ptr.id_}, flags_{ptr.flags_} {
  if (ptr.cached_) {
    cached_ = std::move(ptr.cached_);
  }
}

ObjectPtrBase::~ObjectPtrBase() = default;

ObjId ObjectPtrBase::id() const { return id_; }
ObjFlags ObjectPtrBase::flags() const { return flags_; }
Domain* ObjectPtrBase::domain() const { return domain_; }

void ObjectPtrBase::SetFlags(ObjFlags flags) { flags_ = flags; }

bool ObjectPtrBase::is_valid() const { return id_.is_valid(); }

bool ObjectPtrBase::is_loaded() const { return static_cast<bool>(cached_); }

Ptr<Obj> const& ObjectPtrBase::LoadCached() {
  if (cached_) {
    return cached_;
  }
  if (!is_valid()) {
    return cached_;
  }
  cached_ = DomainGraph{domain_}.LoadRoot(id_);
  if (cached_) {
    flags_ = flags_ & ~ObjFlags::kUnloaded;
  }
  return cached_;
}

Ptr<Obj> const& ObjectPtrBase::LoadCached() const {
  return const_cast<ObjectPtrBase*>(this)->LoadCached();  // NOLINT(*const-cast)
}

void ObjectPtrBase::Save() const {
  if (cached_) {
#if defined(AE_EXP_DIAG)
    auto const t0 = AeExpNowUs();
    AE_EXP_SAVE("begin", "obj_id=%lu",
                static_cast<unsigned long>(id_.id()));
#endif
    DomainGraph{domain_}.SaveRoot(cached_, id_);
#if defined(AE_EXP_DIAG)
    AE_EXP_SAVE("end", "obj_id=%lu duration_us=%lld",
                static_cast<unsigned long>(id_.id()),
                static_cast<long long>(AeExpNowUs() - t0));
#endif
  }
}

void ObjectPtrBase::Reset() {
  cached_.Reset();
  flags_ = flags_ & ObjFlags::kUnloaded;
}

Ptr<Obj> const& ObjectPtrBase::cached() const { return cached_; }
Ptr<Obj>& ObjectPtrBase::cached() { return cached_; }

ObjectPtrBase& ObjectPtrBase::operator=(ObjectPtrBase const& ptr) noexcept {
  if (this != &ptr) {
    domain_ = ptr.domain_;
    id_ = ptr.id_;
    flags_ = ptr.flags_;
    if (ptr.cached_) {
      cached_ = ptr.cached_;
    } else {
      cached_.Reset();
    }
  }
  return *this;
}

ObjectPtrBase& ObjectPtrBase::operator=(ObjectPtrBase&& ptr) noexcept {
  if (this != &ptr) {
    domain_ = ptr.domain_;
    id_ = ptr.id_;
    flags_ = ptr.flags_;

    // Ptr move assignment swaps storage after resetting its destination. Move
    // through a temporary and clear this cache first so ptr cannot receive the
    // cache previously held by this ObjectPtrBase.
    auto cache = std::move(ptr.cached_);
    cached_ = Ptr<Obj>{};
    cached_ = std::move(cache);
  }
  return *this;
}

namespace seri {
using ObjPtrBaseSerializer =
    Serializer<BinaryArchive<DomainBuffer>, ObjectPtrBase>;

SeriResult ObjPtrBaseSerializer::Seri(Archive& archive,
                                      Meta<ObjectPtrBase const> meta) {
  TRY_RESULT((archive.buffer().Write(DataTag{meta.value.id_})));
  TRY_RESULT((archive.buffer().Write(DataTag{meta.value.flags_})));
  if (meta.value.cached_) {
    archive.buffer().domain_graph->SaveRoot(meta.value.cached_, meta.value.id_);
  }
  return Ok{seri::good};
}

SeriResult ObjPtrBaseSerializer::Deseri(Archive& archive,
                                        Meta<ObjectPtrBase> meta) {
  meta.value.domain_ = archive.buffer().domain_graph->domain;
  TRY_RESULT((archive.buffer().Read(DataTag{meta.value.id_})));
  TRY_RESULT((archive.buffer().Read(DataTag{meta.value.flags_})));
  meta.value.cached_.Reset();
  if (meta.value.is_valid() &&
      (meta.value.flags_ & ObjFlags::kUnloadedByDefault) == 0 &&
      (meta.value.flags_ & ObjFlags::kUnloaded) == 0) {
    meta.value.cached_ =
        archive.buffer().domain_graph->LoadRoot(meta.value.id_);
  }
  return Ok{seri::good};
}
}  // namespace seri
}  // namespace ae
