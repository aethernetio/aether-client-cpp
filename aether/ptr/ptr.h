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

#ifndef AETHER_PTR_PTR_H_
#define AETHER_PTR_PTR_H_

#include <cassert>
#include <utility>

// IWYU pragma: begin_keeps
#include "aether/common.h"
#include "aether/type_traits.h"

#include "aether/ptr/rc_ptr.h"
#include "aether/ptr/ref_tree.h"
#include "aether/ptr/ptr_management.h"
#include "aether/reflect/domain_visitor.h"
// IWYU pragma: end_keeps

namespace ae {
class PtrBase {
  friend class PtrViewBase;
  friend struct RefVisitor;

  friend bool operator==(PtrBase const& left, PtrBase const& right);
  friend bool operator!=(PtrBase const& left, PtrBase const& right);

 public:
  struct MoveTag {};

  PtrBase();
  PtrBase(PtrBase const& other);
  PtrBase(PtrBase&& other) noexcept;
  PtrBase(PtrStorageBase* ptr_storage);
  PtrBase(MoveTag, PtrStorageBase* ptr_storage);

  PtrBase& operator=(PtrBase const& other);
  PtrBase& operator=(PtrBase&& other) noexcept;

  explicit operator bool() const noexcept;

 protected:
  void Reset();

  void Increment();
  void Decrement();
  void DecrementRef(std::uint8_t count = 1);
  std::uint8_t DecrementGraphCount();
  RefTree BuildDecrementGraph();
  void BuildDecrementGraphImpl(RefTree& tree, RefTree::Index head_index,
                               std::vector<PtrBase const*> const& children,
                               std::set<std::uintptr_t>& visited);
  std::vector<PtrBase const*> Children() const;
  void Destroy();
  void Free();

  PtrStorageBase* ptr_storage_;
};

bool operator==(PtrBase const& left, PtrBase const& right);
bool operator!=(PtrBase const& left, PtrBase const& right);

template <typename T>
static void PtrDestroy(PtrBase*);
template <typename T>
std::vector<PtrBase const*> PtrChildren(PtrBase*);

/**
 * \brief Pointer - like shared pointer but with ability to create object
 * reference graph and avoid cycle references.
 */
template <typename T>
class Ptr : public PtrBase {
  template <typename U>
  friend class Ptr;
  template <typename U>
  friend class PtrView;
  template <typename U>
  friend class ObjPtr;

  friend void PtrDestroy<T>(PtrBase*);
  friend std::vector<PtrBase const*> PtrChildren<T>(PtrBase*);

 public:
  Ptr() noexcept : PtrBase() {}
  explicit Ptr(std::nullptr_t) noexcept : Ptr() {}

  ~Ptr() { PtrBase::Reset(); }

  // construction of Ptr \see MakePtrFromThis
  explicit Ptr(PtrStorage<T>* ptr_storage) noexcept
      : PtrBase{reinterpret_cast<PtrStorageBase*>(ptr_storage)},
        ptr_{ptr_storage->ptr()} {}

  // construction of Ptr \see MakePtr
  explicit Ptr(MoveTag, PtrStorage<T>* ptr_sotorage) noexcept
      : PtrBase{MoveTag{}, reinterpret_cast<PtrStorageBase*>(ptr_sotorage)},
        ptr_{ptr_sotorage->ptr()} {}

  // Copying
  Ptr(Ptr const& other) noexcept
      : PtrBase{other.ptr_storage_}, ptr_{other.ptr_} {}

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  Ptr(Ptr<U> const& other) noexcept
      : PtrBase{other.ptr_storage_}, ptr_{static_cast<T*>(other.ptr_)} {}

  Ptr& operator=(Ptr const& other) noexcept {
    if (this != &other) {
      PtrBase::operator=(other);
      ptr_ = other.ptr_;
    }
    return *this;
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  Ptr& operator=(Ptr<U> const& other) noexcept {
    PtrBase::operator=(other);
    ptr_ = static_cast<T*>(other.ptr_);
    return *this;
  }

  // Moving
  Ptr(Ptr&& other) noexcept : PtrBase{std::move(other)}, ptr_{other.ptr_} {}

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  Ptr(Ptr<U>&& other) noexcept
      : PtrBase{std::move(other)}, ptr_{static_cast<T*>(other.ptr_)} {}

  Ptr& operator=(Ptr&& other) noexcept {
    if (this != &other) {
      PtrBase::operator=(std::move(other));
      ptr_ = other.ptr_;
    }
    return *this;
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  Ptr& operator=(Ptr<U>&& other) noexcept {
    PtrBase::operator=(std::move(other));
    ptr_ = static_cast<T*>(other.ptr_);
    return *this;
  }

  // Access
  using PtrBase::operator bool;

  [[nodiscard]] T* get() const {
    if (ptr_storage_ == nullptr) {
      return nullptr;
    }
    assert((ptr_ != nullptr) && "Ptr is not initialized");
    return ptr_;
  }
  [[nodiscard]] T* operator->() const noexcept {
    assert((ptr_storage_ != nullptr) && "Dereferencing uninitialized Ptr");
    return get();
  }
  [[nodiscard]] T& operator*() const noexcept {
    assert((ptr_storage_ != nullptr) && "Dereferencing uninitialized Ptr");
    return *get();
  }
  [[nodiscard]] T& operator*() noexcept {
    assert((ptr_storage_ != nullptr) && "Dereferencing uninitialized Ptr");
    return *get();
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  [[nodiscard]] U const* as() const noexcept {
    assert((ptr_storage_ != nullptr) && "Dereferencing uninitialized Ptr");
    return static_cast<U*>(get());
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  [[nodiscard]] U* as() noexcept {
    assert((ptr_storage_ != nullptr) && "Dereferencing uninitialized Ptr");
    return static_cast<U*>(get());
  }

  // Modify
  using PtrBase::Reset;

 private:
  // construction of Ptr \see PtrView::Lock
  explicit Ptr(T* ptr, PtrStorageBase* ptr_storage) noexcept
      : PtrBase{ptr_storage}, ptr_{ptr} {}

  T* ptr_;
};

template <typename T>
static void PtrDestroy(PtrBase* self) {
  auto* _this = reinterpret_cast<Ptr<T>*>(self);
  auto* obj = _this->get();
  obj->~T();
}

template <typename T>
std::vector<PtrBase const*> PtrChildren(PtrBase const* self) {
  auto const* _this = reinterpret_cast<Ptr<T> const*>(self);
  auto const* obj = _this->get();
  assert(obj != nullptr && "Ptr is not initialized");

  auto ref_visitor = RefVisitor{};
  reflect::DomainVisit(*obj, PtrRefDnv{ref_visitor});
  return ref_visitor.children;
}

template <typename T>
static constexpr auto kManageTableForT = ManageTable{
    PtrDestroy<T>,
    PtrChildren<T>,
};

/**
 * \brief Create a Ptr from pointer to this.
 * !There is no reliable way to make it safe.
 * If self_ptr is not a pointer owned by Ptr it's UB.
 * If self_ptr is a Base of Derived which pointer is actually owned by Ptr it's
 * UB.
 */
template <typename T>
Ptr<T> MakePtrFromThis(T* self_ptr) noexcept {
  auto* storage_ptr = reinterpret_cast<std::uint8_t*>(self_ptr) -
                      offsetof(PtrStorage<T>, storage);
  return Ptr{reinterpret_cast<PtrStorage<T>*>(storage_ptr)};
}

/**
 * \brief The only way to create new Ptr
 */
template <typename T, typename... TArgs>
Ptr<T> MakePtr(TArgs&&... args) {
  constexpr auto size = sizeof(PtrStorage<T>);
  static_assert(size < std::numeric_limits<std::uint16_t>::max());
  static_assert(reflect::HasNodeVisitor<T>::value,
                "Type must be reflectable to be used in Ptr");

  auto alloc = std::allocator<std::uint8_t>{};
  auto* storage = reinterpret_cast<PtrStorage<T>*>(alloc.allocate(size));
  assert((storage != nullptr) && "Bad alloc!");
  storage->alloc_size = static_cast<std::uint32_t>(size);
  // first ref already exists! This required to MakePtrFromThis able to work
  // inside T()
  storage->ref_counters.main_refs = 1;
  storage->ref_counters.weak_refs = 1;
  storage->manage_table = &kManageTableForT<T>;
  [[maybe_unused]] auto* ptr =
      new (storage->ptr()) T(std::forward<TArgs>(args)...);
  assert((ptr != nullptr) && "Construction failed!");
  return Ptr<T>{typename PtrBase::MoveTag{}, storage};
}

template <template <typename...> typename T, typename... TArgs>
auto MakePtr(TArgs&&... args) {
  using DeducedT = decltype(T(std::forward<TArgs>(args)...));
  return MakePtr<DeducedT>(std::forward<TArgs>(args)...);
}

}  // namespace ae

namespace ae::reflect {
template <typename T>
struct NodeVisitor<ae::Ptr<T>> {
  using Policy = AnyPolicyMatch;

  void Visit(ae::Ptr<T>& obj, CycleDetector& cycle_detector,
             PtrRefDnv&& visitor) const {
    reflect::ApplyVisitor(obj, cycle_detector, std::move(visitor));
  }

  void Visit(ae::Ptr<T> const& obj, CycleDetector& cycle_detector,
             PtrRefDnv&& visitor) const {
    reflect::ApplyVisitor(obj, cycle_detector, std::move(visitor));
  }

  template <typename Visitor>
  void Visit(ae::Ptr<T> const& obj, CycleDetector& cycle_detector,
             Visitor&& visitor) const {
    if (obj) {
      ApplyVisit(*obj, cycle_detector, std::forward<Visitor>(visitor));
    }
  }

  template <typename Visitor>
  void Visit(ae::Ptr<T>& obj, CycleDetector& cycle_detector,
             Visitor&& visitor) const {
    if (obj) {
      ApplyVisit(*obj, cycle_detector, std::forward<Visitor>(visitor));
    }
  }

  template <typename U, typename Visitor>
  void ApplyVisit(U&& obj, CycleDetector& cycle_detector,
                  Visitor&& visitor) const {
    reflect::ApplyVisitor(std::forward<U>(obj), cycle_detector,
                          std::forward<Visitor>(visitor));
  }
};
}  // namespace ae::reflect

#endif  // AETHER_PTR_PTR_H_
