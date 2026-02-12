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

#include <utility>
#include <cassert>
#include <optional>
#include <type_traits>

#include "aether/ptr/ptr.h"
#include "aether/obj/obj_id.h"
#include "aether/obj/domain.h"
#include "aether/obj/registry.h"
#include "aether/obj/domain_graph.h"
#include "aether/obj/obj_ptr_base.h"
#include "aether/reflect/domain_visitor.h"  // IWYU pragma: keep

namespace ae {

namespace obj_ptr_internal {
template <typename T, typename _ = void>
struct IsObjType : std::false_type {};

template <typename T>
struct IsObjType<T, std::void_t<decltype(T::kClassId)>> : std::true_type {};
}  // namespace obj_ptr_internal

struct ObjProp {
  Domain* domain;
  ObjId id;
};

struct CreateWith {
  CreateWith(Domain* d) : domain{d} { assert(d != nullptr); }
  CreateWith(Domain& d) : domain{&d} {}

  CreateWith&& with_id(ObjId id) && {
    obj_id = id;
    return std::move(*this);
  }

  CreateWith&& with_flags(ObjFlags f) && {
    flags = f;
    return std::move(*this);
  }

  Domain* domain;
  std::optional<ObjId> obj_id;
  std::optional<ObjFlags> flags;
};

template <typename T>
class ObjPtr : public ObjectPtrBase {
  template <typename U>
  friend class ObjPtr;

  template <typename U>
  friend imstream<DomainBufferReader>& operator>>(
      imstream<DomainBufferReader>& is, ObjPtr<U>& ptr);
  template <typename U>
  friend omstream<DomainBufferWriter>& operator<<(
      omstream<DomainBufferWriter>& os, ObjPtr<U> const& ptr);

 public:
  /**
   * \brief Create new object with given arguments.
   */
  template <typename... TArgs>
  static ObjPtr<T> Create(CreateWith create_arg, TArgs&&... args);

  /**
   * \brief Create new object ptr, but leave it in unloaded state.
   */
  static ObjPtr<T> Declare(CreateWith create_arg);

  /**
   * \brief Make an obj ptr from the pointer to the object itself.
   */
  static ObjPtr<T> MakeFromThis(T* self);

  ObjPtr() noexcept : ObjectPtrBase{}, ptr_{} {}

  ~ObjPtr() {
    if (ptr_) {
      ptr_.Reset();
    }
  }

  ObjPtr(Domain* domain, ObjId obj_id, ObjFlags flags)
      : ObjectPtrBase{domain, obj_id, flags} {}

  ObjPtr(Domain* domain, ObjId obj_id, ObjFlags flags, Ptr<T> ptr) noexcept
      : ObjectPtrBase{domain, obj_id, flags}, ptr_{std::move(ptr)} {}

  ObjPtr(ObjPtr const& ptr) noexcept = default;
  ObjPtr(ObjPtr&& ptr) noexcept = default;

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  ObjPtr(ObjPtr<U> ptr) noexcept
      : ObjectPtrBase{static_cast<ObjectPtrBase const&>(ptr)},
        ptr_{std::move(ptr.ptr_)} {}

  ObjPtr& operator=(ObjPtr const& ptr) noexcept = default;
  ObjPtr& operator=(ObjPtr&& ptr) noexcept = default;

  template <typename U, std::enable_if_t<IsAbleToCast<T, U>::value, int> = 0>
  ObjPtr& operator=(ObjPtr<U> ptr) noexcept {
    ptr_.Reset();
    ObjectPtrBase::operator=(static_cast<ObjectPtrBase&>(ptr));
    ptr_ = std::move(ptr.ptr_);
    return *this;
  }

  Ptr<T> const& operator->();
  Ptr<T> const& operator->() const;
  T& operator*();
  T const& operator*() const;

  bool is_valid() const;
  bool is_loaded() const;
  explicit operator bool() const { return is_valid() && is_loaded(); }

  /**
   * \brief Load current object to Ptr
   */
  Ptr<T> const& Load();
  Ptr<T> const& Load() const;
  /**
   * \brief Save current object state
   */
  void Save() const;
  /**
   * \brief Clone current object into new object
   * \param obj_id Optional object ID to use for the new object, if not provided
   * will be generated.
   */
  ObjPtr<T> Clone() const;
  ObjPtr<T> Clone(ObjId obj_id) const;

  template <typename Func>
  auto WithLoaded(Func&& func) -> decltype(auto);
  template <typename Func>
  auto WithLoaded(Func&& func) const -> decltype(auto);

  void Reset();

 private:
  Ptr<T> ptr_;
};

template <typename T>
template <typename... TArgs>
ObjPtr<T> ObjPtr<T>::Create(CreateWith create_arg, TArgs&&... args) {
  static_assert(obj_ptr_internal::IsObjType<T>::value,
                "T Must be an object type");
  auto* domain = create_arg.domain;
  auto obj_id =
      create_arg.obj_id.value_or(domain->MapObj(MakeClassIdentity<T>()));
  auto flags = create_arg.flags.value_or(ObjFlags{});
  static_assert(
      std::is_constructible_v<T, ObjProp, TArgs...>,
      "Class must be constructible with passed arguments and Domain*");
  auto ptr = MakePtr<T>(ObjProp{domain, obj_id}, std::forward<TArgs>(args)...);
  domain->AddObject(obj_id, ptr);

  return ObjPtr<T>(domain, obj_id, flags, std::move(ptr));
}

template <typename T>
ObjPtr<T> ObjPtr<T>::Declare(CreateWith create_arg) {
  static_assert(obj_ptr_internal::IsObjType<T>::value,
                "T Must be an object type");
  auto* domain = create_arg.domain;
  auto obj_id =
      create_arg.obj_id.value_or(domain->MapObj(MakeClassIdentity<T>()));
  auto flags = create_arg.flags.value_or(ObjFlags::kUnloaded);
  return ObjPtr<T>(domain, obj_id, flags);
}

template <typename T>
ObjPtr<T> ObjPtr<T>::MakeFromThis(T* self) {
  static_assert(obj_ptr_internal::IsObjType<T>::value,
                "T Must be an object type");
  auto id = self->obj_id;
  auto* domain = self->domain;
  assert(id.IsValid() && (domain != nullptr) && "Object must be in domain");
  auto ptr = MakePtrFromThis(self);
  assert(ptr && "Object must be valid");
  return ObjPtr<T>(domain, id, {}, std::move(ptr));
}

template <typename T>
Ptr<T> const& ObjPtr<T>::operator->() {
  auto const& ptr = Load();
  assert(ptr && "Dereferencing invalid object");
  return ptr;
}

template <typename T>
Ptr<T> const& ObjPtr<T>::operator->() const {
  auto const& ptr = Load();
  assert(ptr && "Dereferencing invalid object");
  return ptr;
}

template <typename T>
T& ObjPtr<T>::operator*() {
  auto const& ptr = Load();
  assert(ptr && "Dereferencing invalid object");
  return *ptr;
}

template <typename T>
T const& ObjPtr<T>::operator*() const {
  auto const& ptr = Load();
  assert(ptr && "Dereferencing invalid object");
  return *ptr;
}

template <typename T>
bool ObjPtr<T>::is_valid() const {
  return id().IsValid();
}
template <typename T>
bool ObjPtr<T>::is_loaded() const {
  return static_cast<bool>(ptr_);
}

template <typename T>
Ptr<T> const& ObjPtr<T>::Load() {
  // already loaded
  if (ptr_) {
    return ptr_;
  }
  // return empty for invalid object
  if (!is_valid()) {
    return ptr_;
  }
  ptr_ = DomainGraph{domain()}.template LoadPtr<T>(id());
  if (ptr_) {
    flags_ = flags() & ~ObjFlags::kUnloaded;
  }
  return ptr_;
}

template <typename T>
Ptr<T> const& ObjPtr<T>::Load() const {
  return const_cast<ObjPtr<T>*>(this)->Load();
}

template <typename T>
void ObjPtr<T>::Save() const {
  if (!ptr_) {
    return;
  }
  DomainGraph{domain()}.SavePtr(ptr_, id());
}

template <typename T>
ObjPtr<T> ObjPtr<T>::Clone() const {
  return Clone(domain()->MapObj(MakeClassIdentity<T>()));
}

template <typename T>
ObjPtr<T> ObjPtr<T>::Clone(ObjId obj_id) const {
  assert(is_valid() && "Invalid clone ptr");
  assert(obj_id.IsValid() && "Invalid object ID");
  auto ptr = DomainGraph{domain()}.template LoadCopy<T>(id(), obj_id);
  return ObjPtr<T>{domain(), obj_id,
                   static_cast<std::uint8_t>(flags() & ~ObjFlags::kUnloaded),
                   std::move(ptr)};
}

template <typename U, typename Func>
auto WithLoadedImpl(U&& obj_ptr, Func&& func) -> decltype(auto) {
  using InvokeRes =
      std::invoke_result_t<Func, decltype(std::forward<U>(obj_ptr).Load())>;
  using ReturnType = std::conditional_t<std::is_void_v<InvokeRes>, bool,
                                        std::optional<InvokeRes>>;
  auto const& ptr = std::forward<U>(obj_ptr).Load();
  if (!ptr) {
    return ReturnType{};
  }
  if constexpr (std::is_void_v<InvokeRes>) {
    std::invoke(std::forward<Func>(func), ptr);
    return true;
  } else {
    return ReturnType{std::invoke(std::forward<Func>(func), ptr)};
  }
}

template <typename T>
template <typename Func>
auto ObjPtr<T>::WithLoaded(Func&& func) -> decltype(auto) {
  return WithLoadedImpl(*this, std::forward<Func>(func));
}

template <typename T>
template <typename Func>
auto ObjPtr<T>::WithLoaded(Func&& func) const -> decltype(auto) {
  return WithLoadedImpl(*this, std::forward<Func>(func));
}

template <typename T>
void ObjPtr<T>::Reset() {
  ptr_.Reset();
  flags_ = flags() & ObjFlags::kUnloaded;
}

template <typename T>
imstream<DomainBufferReader>& operator>>(imstream<DomainBufferReader>& is,
                                         ObjPtr<T>& ptr) {
  is >> static_cast<ObjectPtrBase&>(ptr);
  if (!(ptr.flags() & ObjFlags::kUnloadedByDefault) &&
      !(ptr.flags() & ObjFlags::kUnloaded)) {
    // Load the object only if it's valid and unloaded flag is not set
    ptr.ptr_ = is.ib_.domain_graph->LoadPtr<T>(ptr.id());
  }
  return is;
}

template <typename T>
omstream<DomainBufferWriter>& operator<<(omstream<DomainBufferWriter>& os,
                                         ObjPtr<T> const& ptr) {
  os << static_cast<ObjectPtrBase const&>(ptr);
  if (ptr.ptr_) {
    os.ob_.domain_graph->SavePtr(ptr.ptr_, ptr.id());
  }
  return os;
}
}  // namespace ae

namespace ae::reflect {
template <typename T>
struct NodeVisitor<ae::ObjPtr<T>> : NodeVisitor<ae::Ptr<T>> {
  using Policy = AnyPolicyMatch;
  using Base = NodeVisitor<ae::Ptr<T>>;

  template <typename Visitor>
  void Visit(ae::ObjPtr<T>& obj_ptr, CycleDetector& cycle_detector,
             Visitor&& visitor) const {
    if (obj_ptr.is_loaded()) {
      Base::Visit(obj_ptr.Load(), cycle_detector,
                  std::forward<Visitor>(visitor));
    }
  }

  template <typename Visitor>
  void Visit(ae::ObjPtr<T> const& obj_ptr, CycleDetector& cycle_detector,
             Visitor&& visitor) const {
    if (obj_ptr.is_loaded()) {
      Base::Visit(obj_ptr.Load(), cycle_detector,
                  std::forward<Visitor>(visitor));
    }
  }
};
}  // namespace ae::reflect

#endif  // AETHER_OBJ_OBJ_PTR_H_
