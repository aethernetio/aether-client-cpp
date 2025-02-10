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
#include <type_traits>

#include "aether/type_traits.h"
#include "aether/obj/domain_tree.h"
#include "aether/obj/version_iterator.h"
#include "aether/obj/visitor_policies.h"

#include "aether/ptr/rc_ptr.h"
#include "aether/ptr/ptr_management.h"

namespace ae {
using PtrVisitorPolicy =
    MultiplexVisitorPolicy<DeepVisitPolicy<ExplicitVisitPolicy>,
                           DeepVisitPolicy<VersionedSaveVisitorPolicy>>;

using PtrDnv =
    DomainNodeVisitor<PtrVisitorPolicy, RefCounterVisitor<PtrVisitorPolicy>&>;

template <typename U>
void PtrDefaultVisitor(PtrDnv& dnv, void* ptr) {
  if (ptr) {
    dnv(*static_cast<U*>(ptr));
  }
}

/**
 * \brief Pointer - like shared pointer but with ability to create object
 * reference graph and avoid cycle references.
 */
template <typename T>
class Ptr {
  template <typename U>
  friend class Ptr;
  template <typename U>
  friend class PtrView;
  template <typename U>
  friend class ObjPtr;

  // Different type comparison.
  template <typename T1, typename T2>
  friend bool operator==(const Ptr<T1>& p1, const Ptr<T2>& p2);
  template <typename T1, typename T2>
  friend bool operator!=(const Ptr<T1>& p1, const Ptr<T2>& p2);

  using VisitorFuncPtr = void (*)(PtrDnv& dnv, void* ptr);

 public:
  struct MoveTag {};
  friend class RefCounterVisitor<PtrVisitorPolicy>;

  Ptr() noexcept : ptr_storage_{nullptr}, ptr_{nullptr} {}
  explicit Ptr(std::nullptr_t) noexcept : Ptr() {}

  ~Ptr() { Reset(); }

  // construction of Ptr \see MakePtr
  explicit Ptr(PtrStorage<T>* rc_storage) noexcept
      : Ptr{reinterpret_cast<PtrStorageBase*>(rc_storage), rc_storage->ptr(),
            PtrDefaultVisitor<T>} {}

  explicit Ptr(MoveTag /*move_tag*/, PtrStorage<T>* rc_storage) noexcept
      : Ptr{MoveTag{}, reinterpret_cast<PtrStorageBase*>(rc_storage),
            rc_storage->ptr(), PtrDefaultVisitor<T>} {}

  // Copying
  Ptr(Ptr const& other) noexcept
      : Ptr{other.ptr_storage_, other.ptr_, other.visitor_func_} {}

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  Ptr(Ptr<U> const& other) noexcept
      : Ptr{other.ptr_storage_, static_cast<T*>(other.ptr_),
            other.visitor_func_} {}

  Ptr& operator=(Ptr const& other) noexcept {
    if (this != &other) {
      Reset();
      ptr_storage_ = other.ptr_storage_;
      ptr_ = other.ptr_;
      visitor_func_ = other.visitor_func_;
      Increment();
    }
    return *this;
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  Ptr& operator=(Ptr<U> const& other) noexcept {
    Reset();
    ptr_storage_ = other.ptr_storage_;
    ptr_ = static_cast<T*>(other.ptr_);
    visitor_func_ = other.visitor_func_;
    Increment();
    return *this;
  }

  // Moving
  Ptr(Ptr&& other) noexcept
      : ptr_storage_{other.ptr_storage_},
        ptr_{other.ptr_},
        visitor_func_{other.visitor_func_} {
    other.ptr_storage_ = nullptr;
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  Ptr(Ptr<U>&& other) noexcept
      : ptr_storage_{other.ptr_storage_},
        ptr_{static_cast<T*>(other.ptr_)},
        visitor_func_{other.visitor_func_} {
    other.ptr_storage_ = nullptr;
  }

  Ptr& operator=(Ptr&& other) noexcept {
    if (this != &other) {
      Reset();
      std::swap(ptr_storage_, other.ptr_storage_);
      ptr_ = other.ptr_;
      visitor_func_ = other.visitor_func_;
    }
    return *this;
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  Ptr& operator=(Ptr<U>&& other) noexcept {
    Reset();
    std::swap(ptr_storage_, other.ptr_storage_);
    ptr_ = static_cast<T*>(other.ptr_);
    visitor_func_ = other.visitor_func_;
    return *this;
  }

  // Access
  [[nodiscard]] T* get() const {
    if (ptr_storage_ == nullptr) {
      return nullptr;
    }
    assert(ptr_ != nullptr);
    return ptr_;
  }
  [[nodiscard]] T* operator->() const noexcept { return get(); }
  [[nodiscard]] T& operator*() const noexcept { return *get(); }
  [[nodiscard]] T& operator*() noexcept { return *get(); }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  [[nodiscard]] U const* as() const noexcept {
    return static_cast<U*>(get());
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  [[nodiscard]] U* as() noexcept {
    return static_cast<U*>(get());
  }

  explicit operator bool() const noexcept { return ptr_storage_ != nullptr; }

  // Reflect
  template <typename Dnv>
  void Visit(Dnv& dnv) {
    auto* ptr = get();
    if (ptr != nullptr) {
      if constexpr (std::is_same_v<Dnv, PtrDnv>) {
        if (visitor_func_ != nullptr) {
          visitor_func_(dnv, ptr);
        }
      } else {
        dnv(*ptr);
      }
    }
  }

  // Modify
  void Reset() {
    if ((ptr_storage_ == nullptr) ||
        (ptr_storage_->ref_counters.main_refs == 0)) {
      return;
    }
    Decrement();

    if (ptr_storage_->ref_counters.main_refs == 0) {
      // prevent cycled ptrviews delete ptr_storage
      ptr_storage_->ref_counters.weak_refs += 1;
      // call the destructor on pointer
      // Note if T is derived from Base, Base must have virtual ~Base to prevent
      // memory leaks
      ptr_->~T();
      ptr_storage_->ref_counters.weak_refs -= 1;
      if (ptr_storage_->ref_counters.weak_refs == 0) {
        auto alloc = std::allocator<std::uint8_t>{};
        alloc.deallocate(reinterpret_cast<std::uint8_t*>(ptr_storage_),
                         static_cast<std::size_t>(ptr_storage_->alloc_size));
      }
    }

    ptr_storage_ = nullptr;
  }

 private:
  explicit Ptr(PtrStorageBase* rc_storage, T* ptr,
               VisitorFuncPtr visitor_func) noexcept
      : ptr_storage_{rc_storage}, ptr_{ptr}, visitor_func_{visitor_func} {
    Increment();
  }

  explicit Ptr(MoveTag /* move_tag */, PtrStorageBase* rc_storage, T* ptr,
               VisitorFuncPtr visitor_func) noexcept
      : ptr_storage_{rc_storage}, ptr_{ptr}, visitor_func_{visitor_func} {
    // No Increment();
  }

  void Increment() {
    if (ptr_storage_ == nullptr) {
      return;
    }
    ptr_storage_->ref_counters.main_refs += 1;
    ptr_storage_->ref_counters.weak_refs += 1;
  }

  void Decrement() {
    if (ptr_storage_ == nullptr) {
      return;
    }
    if constexpr (PtrVisitorPolicy::HasVisitMethod<T, PtrDnv>::value) {
      DecrementGraph();
    } else {
      DecrementRef();
    }
  }

  void DecrementRef(std::uint8_t count = 1) {
    ptr_storage_->ref_counters.main_refs -= count;
    ptr_storage_->ref_counters.weak_refs -= count;
  }

  void DecrementGraph() {
    // count all reference reachable from current ptr
    // iterate through all objects with domain node visitor in save like mode

    CycleDetector cycle_detector;
    RefCounterVisitor<PtrVisitorPolicy>::ObjMap obj_map;
    auto [inserted, _] =
        obj_map.emplace(static_cast<void const*>(ptr_),
                        RefTree{static_cast<void const*>(ptr_),
                                ptr_storage_->ref_counters.main_refs});
    inserted->second.reachable_ref_count++;

    RefCounterVisitor<PtrVisitorPolicy> ref_counter_visitor{
        cycle_detector, obj_map, *(inserted->second.children)};

    DomainTree<PtrVisitorPolicy>::Visit(cycle_detector, *ptr_,
                                        ref_counter_visitor);

    // if any object has external references, obj_reference >
    // reachable_reference
    // we are able to remove current object
    bool safe_to_delete = true;
    for (auto& [o, ref] : obj_map) {
      if (o == ptr_) {
        if (ref.ref_count != ref.reachable_ref_count) {
          safe_to_delete = false;
          break;
        }
      } else if (ref.ref_count > ref.reachable_ref_count) {
        // check if ptr reachable form ref obj;
        if (ref.IsReachable(ptr_)) {
          safe_to_delete = false;
          break;
        }
      }
    }

    if (safe_to_delete) {
      DecrementRef(ptr_storage_->ref_counters.main_refs);
    } else {
      DecrementRef();
    }
  }

  PtrStorageBase* ptr_storage_;
  T* ptr_;
  VisitorFuncPtr visitor_func_;
};

template <typename T1, typename T2>
bool operator==(const Ptr<T1>& p1, const Ptr<T2>& p2) {
  return static_cast<void*>(p1.ptr_storage_) ==
         static_cast<void*>(p2.ptr_storage_);
}

template <typename T1, typename T2>
bool operator!=(const Ptr<T1>& p1, const Ptr<T2>& p2) {
  return !(p1 == p2);
}

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
  static_assert(size < std::numeric_limits<std::uint32_t>::max());

  auto alloc = std::allocator<std::uint8_t>{};
  auto* storage = reinterpret_cast<PtrStorage<T>*>(alloc.allocate(size));
  assert(storage != nullptr);
  storage->alloc_size = static_cast<std::uint32_t>(size);
  // first ref already exists! This required to MakePtrFromThis able to work
  // inside T()
  storage->ref_counters = {1, 1};
  auto* ptr = new (storage->ptr()) T(std::forward<TArgs>(args)...);
  if (ptr == nullptr) {
    alloc.deallocate(reinterpret_cast<std::uint8_t*>(storage), size);
    return Ptr<T>{};
  }
  return Ptr<T>{typename Ptr<T>::MoveTag{}, storage};
}

template <template <typename...> typename T, typename... TArgs>
auto MakePtr(TArgs&&... args) {
  using DeducedT = decltype(T(std::forward<TArgs>(args)...));
  return MakePtr<DeducedT>(std::forward<TArgs>(args)...);
}

}  // namespace ae

#endif  // AETHER_PTR_PTR_H_
