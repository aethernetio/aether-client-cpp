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

#ifndef AETHER_OBJ_PTR_H_
#define AETHER_OBJ_PTR_H_

#include <type_traits>
#include <cassert>
#include <utility>

#include "aether/obj/domain_tree.h"
#include "aether/obj/version_iterator.h"
#include "aether/obj/visitor_policies.h"
#include "aether/type_traits.h"

#include "aether/obj/ptr_management.h"

namespace ae {
using PtrVisitorPolicy =
    MultiplexVisitorPolicy<DeepVisitPolicy<ExplicitVisitPolicy>,
                           DeepVisitPolicy<VersionedSaveVisitorPolicy>>;

using PtrDnv =
    DomainNodeVisitor<PtrVisitorPolicy, RefCounterVisitor<PtrVisitorPolicy>&>;

template <typename U>
void PtrDefaultDeleter(void* ptr) {
  if (ptr) {
    delete static_cast<U*>(ptr);
  }
}

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

 public:
  friend class RefCounterVisitor<PtrVisitorPolicy>;

  Ptr() noexcept : shared_pointer_{} {}
  explicit Ptr(std::nullptr_t) noexcept : shared_pointer_{} {};

  explicit Ptr(T* ptr, PointerRefManager::Deleter deleter =
                           PtrDefaultDeleter<T>) noexcept
      : shared_pointer_{ptr, PtrDefaultVisitor<T>, deleter} {
    IncrementRef();
  }

  template <typename U, std::enable_if_t<IsAbleToCast<T, U>::value, int> = 0>
  explicit Ptr(U* ptr, PointerRefManager::Deleter deleter =
                           PtrDefaultDeleter<U>) noexcept
      : shared_pointer_{static_cast<T*>(ptr), PtrDefaultVisitor<T>, deleter} {
    IncrementRef();
  }

  Ptr(Ptr const& ptr) noexcept : shared_pointer_{ptr.shared_pointer_} {
    IncrementRef();
  }

  Ptr(Ptr&& ptr) noexcept : shared_pointer_{std::move(ptr.shared_pointer_)} {
    ptr.shared_pointer_.Leak();
  }

  template <typename U, std::enable_if_t<IsAbleToCast<T, U>::value, int> = 0>
  Ptr(Ptr<U> const& ptr) noexcept : Ptr{ptr.template Cast<T>()} {}

  template <typename U, std::enable_if_t<IsAbleToCast<T, U>::value, int> = 0>
  Ptr(Ptr<U>&& ptr) noexcept : Ptr{std::move(ptr).template MoveCast<T>()} {}

  ~Ptr() { Reset(); }

  Ptr& operator=(T* ptr) noexcept {
    Reset();
    shared_pointer_ =
        SharedPointer{ptr, PtrDefaultVisitor<T>, PtrDefaultDeleter<T>};
    IncrementRef();

    return *this;
  }

  template <typename U, std::enable_if_t<IsAbleToCast<T, U>::value, int> = 0>
  Ptr& operator=(U* ptr) noexcept {
    Reset();
    shared_pointer_ = SharedPointer{static_cast<T*>(ptr), PtrDefaultVisitor<T>,
                                    PtrDefaultDeleter<U>};
    IncrementRef();

    return *this;
  }

  Ptr& operator=(Ptr const& ptr) noexcept {
    if (this != &ptr) {
      Reset();
      shared_pointer_ = ptr.shared_pointer_;
      IncrementRef();
    }
    return *this;
  }

  Ptr& operator=(Ptr&& ptr) noexcept {
    if (this != &ptr) {
      Reset();
      shared_pointer_ = std::move(ptr.shared_pointer_);
      ptr.shared_pointer_.Leak();
    }
    return *this;
  }

  template <typename U, std::enable_if_t<IsAbleToCast<T, U>::value, int> = 0>
  Ptr& operator=(Ptr<U> const& ptr) noexcept {
    return operator=(ptr.template Cast<T>());
  }

  template <typename U, std::enable_if_t<IsAbleToCast<T, U>::value, int> = 0>
  Ptr& operator=(Ptr<U>&& ptr) noexcept {
    return operator=(std::move(ptr).template MoveCast<T>());
  }

  // create a copy with different object type
  template <typename U>
  Ptr<U> Cast() const {
    return Ptr<U>{shared_pointer_};
  }

  // move this to a copy with different object type
  template <typename U>
  Ptr<U> MoveCast() && {
    auto ptr = Ptr<U>{shared_pointer_};
    DecrementRef();
    shared_pointer_.Leak();
    return ptr;
  }

  explicit operator bool() const noexcept {
    return shared_pointer_.pointer() != nullptr;
  }

  T* get() const {
    auto* ptr_manager = shared_pointer_.pointer_manager();
    if (!ptr_manager) {
      return nullptr;
    }
    if (ptr_manager->ref_counter() == 0) {
      return nullptr;
    }
    return shared_pointer_.pointer();
  }
  T* operator->() const { return get(); }
  T& operator*() const { return *get(); }
  T& operator*() { return *get(); }

  template <typename U, std::enable_if_t<IsAbleToCast<T, U>::value, int> = 0>
  U const* as() const {
    return static_cast<U*>(get());
  }

  template <typename U, std::enable_if_t<IsAbleToCast<T, U>::value, int> = 0>
  U* as() {
    return static_cast<U*>(get());
  }

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    auto* ptr = get();
    if (ptr) {
      if constexpr (std::is_same_v<Dnv, PtrDnv>) {
        shared_pointer_.visitor()(dnv, ptr);
      } else {
        dnv(*ptr);
      }
    }
  }

  void Reset() {
    auto* ptr_manager = shared_pointer_.pointer_manager();
    if (!ptr_manager) {
      return;
    }
    if (ptr_manager->ref_counter() == 0) {
      return;
    }

    auto* ptr = shared_pointer_.pointer();
    if (!ptr) {
      return;
    }

    if (ptr_manager->ref_counter() == 1) {
      // edge case - it's the only ptr to this object
      DecrementRef();
    } else {
      DecrementGraph();
    }

    if (ptr_manager->ref_counter() == 0) {
      ptr_manager->Delete(ptr);
    }
    shared_pointer_.Reset();
  }

 private:
  explicit Ptr(SharedPointer<T, PtrDnv> shared_pointer) noexcept
      : shared_pointer_{std::move(shared_pointer)} {
    IncrementRef();
  }

  void IncrementRef() {
    auto* ptr_manager = shared_pointer_.pointer_manager();
    if (!ptr_manager) {
      return;
    }
    ptr_manager->Increment();
  }

  void DecrementRef(std::uint16_t count = 1) {
    auto* ptr_manager = shared_pointer_.pointer_manager();
    if (!ptr_manager) {
      return;
    }
    ptr_manager->Decrement(count);
  }

  void DecrementGraph() {
    auto* ptr_manager = shared_pointer_.pointer_manager();
    auto* ptr = shared_pointer_.pointer();

    using Dnv = DomainNodeVisitor<PtrVisitorPolicy,
                                  RefCounterVisitor<PtrVisitorPolicy>&>;

    // count all reference reachable from current ptr
    // iterate through all objects with domain node visitor in save like
    // mode
    if constexpr (PtrVisitorPolicy::HasVisitMethod<T, Dnv>::value) {
      CycleDetector cycle_detector;
      RefCounterVisitor<PtrVisitorPolicy>::ObjMap obj_map;
      auto [inserted, _] = obj_map.emplace(
          static_cast<void const*>(ptr),
          RefTree{static_cast<void const*>(ptr), ptr_manager->ref_counter()});
      inserted->second.reachable_ref_count++;

      RefCounterVisitor<PtrVisitorPolicy> ref_counter_visitor{
          cycle_detector, obj_map, *(inserted->second.children)};

      DomainTree<PtrVisitorPolicy>::Visit(cycle_detector, *ptr,
                                          ref_counter_visitor);

      // if any object has external references, obj_reference >
      // reachable_reference
      // we ara able to remove current object
      bool safe_to_delete = true;
      for (auto& [o, ref] : obj_map) {
        if (o == ptr) {
          if (ref.ref_count != ref.reachable_ref_count) {
            safe_to_delete = false;
            break;
          }
        } else if (ref.ref_count > ref.reachable_ref_count) {
          // check if ptr reachable form ref obj;
          if (ref.IsReachable(ptr)) {
            safe_to_delete = false;
            break;
          }
        }
      }

      if (safe_to_delete) {
        DecrementRef(ptr_manager->ref_counter());
      } else {
        DecrementRef();
      }
    } else {
      // if visitor not implemented just decrement
      DecrementRef();
    }
  }

  SharedPointer<T, PtrDnv> shared_pointer_;
};

template <typename T1, typename T2>
bool operator==(const Ptr<T1>& p1, const Ptr<T2>& p2) {
  return p1.get() == p2.get();
}

template <typename T1, typename T2>
bool operator!=(const Ptr<T1>& p1, const Ptr<T2>& p2) {
  return !(p1 == p2);
}

template <typename T, typename... TArgs>
auto MakePtr(TArgs&&... args) {
  auto data_pointer = new T{std::forward<TArgs>(args)...};
  auto ptr = Ptr<std::decay_t<std::remove_pointer_t<decltype(data_pointer)>>>{
      data_pointer};
  // in case of error delete allocated memory
  if (!ptr) {
    delete data_pointer;
  }
  return ptr;
}

template <template <typename...> typename T, typename... TArgs>
auto MakePtr(TArgs&&... args) {
  auto data_pointer = new T{std::forward<TArgs>(args)...};
  auto ptr = Ptr<std::decay_t<std::remove_pointer_t<decltype(data_pointer)>>>{
      data_pointer};
  // in case of error delete allocated memory
  if (!ptr) {
    delete data_pointer;
  }
  return ptr;
}

}  // namespace ae

#endif  // AETHER_OBJ_PTR_H_
