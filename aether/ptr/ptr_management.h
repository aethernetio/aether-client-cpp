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

#ifndef AETHER_PTR_PTR_MANAGEMENT_H_
#define AETHER_PTR_PTR_MANAGEMENT_H_

#include <type_traits>
#include <cstdint>
#include <cassert>
#include <set>
#include <map>
#include <memory>

#include "aether/ptr/rc_ptr.h"
#include "aether/obj/domain_tree.h"

namespace ae {

// PtrStorageBase* is used in general case, but PtrStorage<T>* in case there T
// is known
struct PtrStorageBase {
  RefCounters ref_counters;
  std::uint32_t alloc_size;
};

template <typename T>
class PtrStorage {
 public:
  [[nodiscard]] auto* ptr() noexcept { return storage.ptr(); }
  [[nodiscard]] auto* ptr() const noexcept { return storage.ptr(); }

  RefCounters ref_counters;
  std::uint32_t alloc_size;
  Storage<T> storage;
};

template <typename T>
class Ptr;

template <typename T, typename _ = void>
struct IsPtr : std::false_type {};

template <typename T, template <typename...> typename UPtr>
struct IsPtr<UPtr<T>, std::enable_if_t<std::is_base_of_v<Ptr<T>, UPtr<T>>>>
    : std::true_type {};

struct RefTree {
  using RefTreeChildren = std::set<RefTree*>;

  RefTree();
  RefTree(void const* pointer, std::uint8_t ref_count);
  RefTree(RefTree const& other) = delete;
  RefTree(RefTree&& other) noexcept = default;

  bool IsReachable(void const* p, std::set<RefTree*>&& visited = {});

  void const* pointer;
  std::uint8_t ref_count;
  std::uint8_t reachable_ref_count;
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
    auto const* obj_ptr = static_cast<void const*>(obj.get());
    if (!obj_ptr) {
      return false;
    }
    auto const& refs = obj.ptr_storage_->ref_counters;

    auto [it, _] = obj_map_.try_emplace(obj_ptr, obj_ptr, refs.main_refs);
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

#endif  // AETHER_PTR_PTR_MANAGEMENT_H_
