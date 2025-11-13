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

#ifndef AETHER_OBJ_OBJ_PTR_H_
#define AETHER_OBJ_OBJ_PTR_H_

#include <cstddef>
#include <utility>
#include <type_traits>

#include "aether/ptr/ptr.h"
#include "aether/obj/obj_id.h"
#include "aether/reflect/domain_visitor.h"  // IWYU pragma: keep

namespace ae {
class Obj;

template <typename T, typename _ = void>
struct IsObjType : std::false_type {};

template <typename T>
struct IsObjType<T, std::void_t<decltype(T::kClassId)>> : std::true_type {};

class ObjectPtrBase {
 public:
  ObjectPtrBase();
  explicit ObjectPtrBase(Obj* ptr);

  ObjectPtrBase(ObjectPtrBase const& ptr) noexcept;
  ObjectPtrBase(ObjectPtrBase&& ptr) noexcept;

  void SetId(ObjId id);
  void SetFlags(ObjFlags flags);

  ObjId GetId() const;
  ObjFlags GetFlags() const;

 protected:
  ObjectPtrBase& operator=(Obj* ptr) noexcept;
  ObjectPtrBase& operator=(ObjectPtrBase const& ptr) noexcept;
  ObjectPtrBase& operator=(ObjectPtrBase&& ptr) noexcept;

 private:
  Obj* ptr_ = nullptr;
  ObjId id_;
  ObjFlags flags_;
};

template <typename T>
class ObjPtr : public Ptr<T>, public ObjectPtrBase {
  template <typename U>
  friend class ObjPtr;

 public:
  ObjPtr() noexcept : Ptr<T>{}, ObjectPtrBase{} {}
  explicit ObjPtr(std::nullptr_t) noexcept : Ptr<T>{nullptr}, ObjectPtrBase{} {}

  ~ObjPtr() { Ptr<T>::Reset(); }

  ObjPtr(Ptr<T> const& ptr) noexcept
      : Ptr<T>{ptr}, ObjectPtrBase(Ptr<T>::get()) {}
  ObjPtr(Ptr<T>&& ptr) noexcept
      : Ptr<T>(std::move(ptr)), ObjectPtrBase(Ptr<T>::get()) {}

  ObjPtr(ObjPtr const& ptr) noexcept : Ptr<T>{ptr}, ObjectPtrBase{ptr} {}
  ObjPtr(ObjPtr&& ptr) noexcept
      : Ptr<T>{std::move(ptr)}, ObjectPtrBase{std::move(ptr)} {}

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  ObjPtr(ObjPtr<U> const& ptr) noexcept : Ptr<T>{ptr}, ObjectPtrBase{ptr} {}

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  ObjPtr(ObjPtr<U>&& ptr) noexcept
      : Ptr<T>(std::move(ptr)), ObjectPtrBase(std::move(ptr)) {}

  ObjPtr& operator=(ObjPtr const& ptr) noexcept {
    Ptr<T>::operator=(ptr);
    ObjectPtrBase::operator=(ptr);
    return *this;
  }
  ObjPtr& operator=(ObjPtr&& ptr) noexcept {
    Ptr<T>::operator=(std::move(ptr));
    ObjectPtrBase::operator=(std::move(ptr));
    return *this;
  }

  template <typename U, std::enable_if_t<IsAbleToCast<T, U>::value, int> = 0>
  ObjPtr& operator=(ObjPtr<U> const& ptr) noexcept {
    Ptr<T>::operator=(ptr);
    ObjectPtrBase::operator=(ptr);
    return *this;
  }
  template <typename U, std::enable_if_t<IsAbleToCast<T, U>::value, int> = 0>
  ObjPtr& operator=(ObjPtr<U>&& ptr) noexcept {
    Ptr<T>::operator=(std::move(ptr));
    ObjectPtrBase::operator=(std::move(ptr));
    return *this;
  }
};
}  // namespace ae

namespace ae::reflect {
template <typename T>
struct NodeVisitor<ae::ObjPtr<T>> : NodeVisitor<ae::Ptr<T>> {
  using Policy = AnyPolicyMatch;

  template <typename Visitor>
  void Visit(ae::ObjPtr<T>& obj_ptr, CycleDetector& cycle_detector,
             Visitor&& visitor) const {
    NodeVisitor<ae::Ptr<T>>::Visit(obj_ptr, cycle_detector,
                                   std::forward<Visitor>(visitor));
  }

  template <typename Visitor>
  void Visit(ae::ObjPtr<T> const& obj_ptr, CycleDetector& cycle_detector,
             Visitor&& visitor) const {
    NodeVisitor<ae::Ptr<T>>::Visit(obj_ptr, cycle_detector,
                                   std::forward<Visitor>(visitor));
  }
};
}  // namespace ae::reflect

#endif  // AETHER_OBJ_OBJ_PTR_H_
