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

#include <set>
#include <map>
#include <memory>
#include <cstdint>
#include <cassert>
#include <type_traits>

#include "aether/ptr/rc_ptr.h"
#include "aether/reflect/domain_visitor.h"

namespace ae {
struct PtrRefcounters {
  std::uint8_t main_refs = 0;
  std::uint8_t weak_refs = 0;
};

// PtrStorageBase* is used in general case, but PtrStorage<T>* in case there T
// is known
struct PtrStorageBase {
  PtrRefcounters ref_counters;
  std::uint16_t alloc_size;
};

template <typename T>
class PtrStorage {
 public:
  [[nodiscard]] auto* ptr() noexcept { return storage.ptr(); }
  [[nodiscard]] auto* ptr() const noexcept { return storage.ptr(); }

  PtrRefcounters ref_counters;
  std::uint16_t alloc_size;
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
  RefTree(void const* ptr, std::uint8_t ref_count);

  AE_CLASS_NO_COPY_MOVE(RefTree);

  bool IsReachable(void const* ptr, std::set<RefTree*>&& visited = {});

  void const* pointer;
  std::uint8_t ref_count;
  std::uint8_t reachable_ref_count;
  RefTreeChildren children;
};

using ObjMap = std::map<void const*, std::unique_ptr<RefTree>>;
struct RefCounterVisitor;

using PtrDnv =
    reflect::DomainNodeVisitor<RefCounterVisitor, reflect::VisitPolicy::kDeep>;

struct RefCounterVisitor {
  template <typename U>
  std::enable_if_t<IsPtr<U>::value, bool> operator()(U const& obj) {
    if (ignore_obj == static_cast<void const*>(&obj)) {
      return false;
    }

    auto const* obj_ptr = static_cast<void const*>(obj.get());
    if (!obj_ptr) {
      return false;
    }
    auto const& refs = obj.ptr_storage_->ref_counters;
    if (refs.main_refs == 0) {
      return false;
    }

    auto [it, _] = obj_map.emplace(
        obj_ptr, std::make_unique<RefTree>(obj_ptr, refs.main_refs));
    auto& ref_counter = *it->second;
    ref_counter.reachable_ref_count++;
    assert(ref_counter.reachable_ref_count <= ref_counter.ref_count);
    children.insert(&ref_counter);

    auto next_visitor = RefCounterVisitor{cycle_detector, obj_map,
                                          ref_counter.children, ignore_obj};
    obj.VisitDeeper(next_visitor);
    return false;
  }

  template <typename U>
  std::enable_if_t<!IsPtr<U>::value> operator()(U const& /* obj */) {}

  template <typename U>
  void Visit(U* obj) {
    reflect::DomainVisit(cycle_detector, *obj, PtrDnv{*this});
  }

  reflect::CycleDetector& cycle_detector;
  ObjMap& obj_map;
  RefTree::RefTreeChildren& children;
  void const* ignore_obj;
};

class PtrGraphBuilder {
 public:
  template <typename T>
  static ObjMap BuildGraph(T& obj, PtrRefcounters const& ref_counters,
                           void const* ignore_obj) {
    auto obj_map = ObjMap{};
    auto [inserted, _] = obj_map.emplace(
        &obj, std::make_unique<RefTree>(&obj, ref_counters.main_refs));
    inserted->second->reachable_ref_count++;

    auto cycle_detector = reflect::CycleDetector{};
    auto visitor = RefCounterVisitor{cycle_detector, obj_map,
                                     inserted->second->children, ignore_obj};
    reflect::DomainVisit(cycle_detector, obj, PtrDnv{visitor});
    return obj_map;
  }
};

}  // namespace ae

#endif  // AETHER_PTR_PTR_MANAGEMENT_H_
