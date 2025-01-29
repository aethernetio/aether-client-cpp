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

#ifndef AETHER_OBJ_PTR_MANAGEMENT_H_
#define AETHER_OBJ_PTR_MANAGEMENT_H_

#include <type_traits>
#include <cstdint>
#include <cassert>
#include <set>
#include <map>
#include <memory>

#include "aether/obj/domain_tree.h"

namespace ae {

template <typename T>
class Ptr;

template <typename T, typename _ = void>
struct IsPtr : std::false_type {};

template <typename T, template <typename...> typename UPtr>
struct IsPtr<UPtr<T>, std::enable_if_t<std::is_base_of_v<Ptr<T>, UPtr<T>>>>
    : std::true_type {};

class PointerRefManager {
 public:
  using Deleter = void (*)(void*);

  explicit PointerRefManager() noexcept;

  void Increment(std::uint16_t count = 1) noexcept;
  void IncrementManage(std::uint16_t count = 1) noexcept;

  void Decrement(std::uint16_t count = 1) noexcept;
  void DecrementManage(std::uint16_t count = 1) noexcept;

  std::uint16_t ref_counter() const noexcept;
  std::uint16_t manage_counter() const noexcept;

 private:
  // count references
  std::uint16_t ref_counter_;
  // count view references
  std::uint16_t manage_counter_;
};

class PointerManager : public PointerRefManager {
 public:
  explicit PointerManager(Deleter deleter) noexcept;

  void ReplaceDeleter(Deleter deleter) noexcept;

  void Delete(void* ptr) noexcept;

 private:
  Deleter deleter_;
};

template <typename T, typename Dnv>
class SharedPointer {
  template <typename U, typename UDnv>
  friend class SharedPointer;

 public:
  using Visitor = void (*)(Dnv&, void*);

  SharedPointer() noexcept
      : pointer_manager_{nullptr}, pointer_{nullptr}, visitor_{} {}
  SharedPointer(T* pointer, Visitor visitor,
                PointerRefManager::Deleter deleter) noexcept
      : pointer_manager_(new(std::nothrow) PointerManager{deleter}),
        pointer_{pointer},
        visitor_{visitor} {
    if (pointer_manager_) {
      pointer_manager_->IncrementManage();
    }
  }

  SharedPointer(SharedPointer const& other) noexcept
      : pointer_manager_{other.pointer_manager_},
        pointer_{other.pointer_},
        visitor_{other.visitor_} {
    if (pointer_manager_) {
      pointer_manager_->IncrementManage();
    }
  }

  SharedPointer(SharedPointer&& other) noexcept
      : pointer_manager_{other.pointer_manager_},
        pointer_{other.pointer_},
        visitor_{other.visitor_} {
    other.pointer_manager_ = nullptr;
    other.pointer_ = nullptr;
  }

  template <typename U>
  SharedPointer(SharedPointer<U, Dnv> const& other) noexcept
      : pointer_manager_{other.pointer_manager_},
        pointer_{static_cast<T*>(other.pointer_)},
        visitor_{other.visitor_} {
    if (pointer_manager_) {
      pointer_manager_->IncrementManage();
    }
  }
  template <typename U>
  SharedPointer(SharedPointer<U, Dnv>&& other) noexcept
      : pointer_manager_{other.pointer_manager_},
        pointer_{static_cast<T*>(other.pointer_)},
        visitor_{other.visitor_} {
    other.pointer_manager_ = nullptr;
    other.pointer_ = nullptr;
  }

  ~SharedPointer() noexcept { Reset(); }

  SharedPointer& operator=(SharedPointer const& other) {
    if (this != &other) {
      Reset();
      pointer_manager_ = other.pointer_manager_;
      pointer_ = other.pointer_;
      visitor_ = other.visitor_;
      if (pointer_manager_) {
        pointer_manager_->IncrementManage();
      }
    }
    return *this;
  }
  SharedPointer& operator=(SharedPointer&& other) {
    if (this != &other) {
      Reset();
      pointer_manager_ = other.pointer_manager_;
      pointer_ = other.pointer_;
      visitor_ = other.visitor_;
      other.pointer_manager_ = nullptr;
      other.pointer_ = nullptr;
      other.visitor_ = nullptr;
    }
    return *this;
  }

  void Leak() noexcept {
    if (pointer_manager_) {
      pointer_manager_->DecrementManage();
    }
    pointer_manager_ = nullptr;
    pointer_ = nullptr;
    visitor_ = nullptr;
  }

  void Reset() noexcept {
    if (!pointer_manager_) {
      return;
    }
    pointer_manager_->DecrementManage();
    if (pointer_manager_->manage_counter() == 0) {
      delete pointer_manager_;
    }
    pointer_manager_ = nullptr;
    pointer_ = nullptr;
    visitor_ = nullptr;
  }

  PointerManager* pointer_manager() const { return pointer_manager_; }
  T* pointer() const { return pointer_; }

  Visitor visitor() const { return visitor_; }

 private:
  PointerManager* pointer_manager_;
  T* pointer_;
  Visitor visitor_;
};

struct RefTree {
  using RefTreeChildren = std::set<RefTree*>;

  RefTree();
  RefTree(void const* pointer, std::uint32_t ref_count);
  RefTree(RefTree const& other) = delete;
  RefTree(RefTree&& other) noexcept = default;

  bool IsReachable(void const* p, std::set<RefTree*>&& visited = {});

  void const* pointer;
  std::uint32_t ref_count;
  std::uint32_t reachable_ref_count;
  std::unique_ptr<RefTreeChildren> children;
};

template <typename TVisitorPolicy>
class RefCounterVisitor {
 public:
  using ObjMap = std::map<void const*, RefTree>;

  explicit RefCounterVisitor(CycleDetector& cycle_detector, ObjMap& obj_map,
                             RefTree::RefTreeChildren& obj_references)
      : cycle_detector_{cycle_detector},
        obj_map_{obj_map},
        obj_references_{obj_references} {}

  template <typename U>
  std::enable_if_t<IsPtr<U>::value, bool> operator()(U const& obj) {
    auto* obj_ptr = static_cast<void const*>(obj.get());
    if (!obj_ptr) {
      return false;
    }
    auto* pm = obj.shared_pointer_.pointer_manager();

    auto [it, _] = obj_map_.try_emplace(obj_ptr, obj_ptr, pm->ref_counter());
    auto& ref_counter = it->second;
    ref_counter.reachable_ref_count++;

    obj_references_.insert(&ref_counter);

    RefCounterVisitor visitor{cycle_detector_, obj_map_, *ref_counter.children};
    DomainTree<TVisitorPolicy>::Visit(cycle_detector_, const_cast<U&>(obj),
                                      visitor);
    return false;
  }

 private:
  CycleDetector& cycle_detector_;
  ObjMap& obj_map_;
  RefTree::RefTreeChildren& obj_references_;
};

}  // namespace ae

#endif  // AETHER_OBJ_PTR_MANAGEMENT_H_
