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

#include "aether/obj/obj_ptr.h"

#include "aether/obj/obj.h"
#include "aether/obj/obj_id.h"

namespace ae {

ObjectPtrBase::ObjectPtrBase() : id_{}, flags_{ObjFlags::kUnloaded} {}
ObjectPtrBase::ObjectPtrBase(Obj* ptr) : ptr_{ptr}, id_{}, flags_{} {
  if (ptr_) {
    id_ = ptr_->id_;
    flags_ = ptr_->flags_;
  }
}

ObjectPtrBase::ObjectPtrBase(ObjectPtrBase const& ptr) noexcept = default;

ObjectPtrBase::ObjectPtrBase(ObjectPtrBase&& ptr) noexcept = default;

void ObjectPtrBase::SetId(ObjId id) {
  id_ = id;
  if (ptr_ != nullptr) {
    ptr_->id_ = id;
  }
}
void ObjectPtrBase::SetFlags(ObjFlags flags) {
  flags_ = flags;
  if (ptr_ != nullptr) {
    ptr_->flags_ = flags;
  }
}

ObjId ObjectPtrBase::GetId() const {
  return (ptr_ != nullptr) ? ptr_->id_ : id_;
}
ObjFlags ObjectPtrBase::GetFlags() const {
  return (ptr_ != nullptr) ? ptr_->flags_ : flags_;
}

ObjectPtrBase& ObjectPtrBase::operator=(Obj* ptr) noexcept {
  ptr_ = ptr;
  if (ptr_ != nullptr) {
    id_ = ptr_->id_;
    flags_ = ptr_->flags_;
  } else {
    id_ = {};
    flags_ = {};
  }
  return *this;
}

ObjectPtrBase& ObjectPtrBase::operator=(ObjectPtrBase const& ptr) noexcept =
    default;

ObjectPtrBase& ObjectPtrBase::operator=(ObjectPtrBase&& ptr) noexcept = default;

}  // namespace ae
