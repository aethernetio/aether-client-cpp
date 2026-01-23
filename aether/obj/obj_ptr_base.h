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

#ifndef AETHER_OBJ_OBJ_PTR_BASE_H_
#define AETHER_OBJ_OBJ_PTR_BASE_H_

#include "aether/obj/obj_id.h"
#include "aether/obj/domain.h"

namespace ae {

class ObjectPtrBase {
  friend imstream<DomainBufferReader>& operator>>(
      imstream<DomainBufferReader>& is, ObjectPtrBase& ptr);

  friend omstream<DomainBufferWriter>& operator<<(
      omstream<DomainBufferWriter>& os, ObjectPtrBase const& ptr);

 public:
  ObjectPtrBase();
  ObjectPtrBase(Domain* domain, ObjId obj_id, ObjFlags flags);

  ObjectPtrBase(ObjectPtrBase const& ptr) noexcept;
  ObjectPtrBase& operator=(ObjectPtrBase const& ptr) noexcept;

  ObjId id() const;
  ObjFlags flags() const;
  Domain* domain() const;

  void SetFlags(ObjFlags flags);

 protected:
  Domain* domain_;
  ObjId id_;
  ObjFlags flags_;
};

imstream<DomainBufferReader>& operator>>(imstream<DomainBufferReader>& is,
                                         ObjectPtrBase& ptr);

omstream<DomainBufferWriter>& operator<<(omstream<DomainBufferWriter>& os,
                                         ObjectPtrBase const& ptr);
}  // namespace ae

#endif  // AETHER_OBJ_OBJ_PTR_BASE_H_
