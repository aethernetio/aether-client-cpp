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

#include <cstdint>
#include <cassert>
#include <type_traits>

#include "aether/ptr/ref_tree.h"
#include "aether/types/aligned_storage.h"
#include "aether/reflect/domain_visitor.h"

namespace ae {
template <typename T>
class Ptr;

namespace ptr_management_internal {
template <typename T, typename _ = void>
struct IsPtr : std::false_type {};

template <typename T, template <typename...> typename UPtr>
struct IsPtr<UPtr<T>, std::enable_if_t<std::is_base_of_v<Ptr<T>, UPtr<T>>>>
    : std::true_type {};
}  // namespace ptr_management_internal

struct PtrRefcounters {
  std::uint8_t main_refs = 0;
  std::uint8_t weak_refs = 0;
};

class PtrBase;
struct RefCounterVisitor;

// function table for managing Ptr<T> from PtrBase
struct ManageTable {
  void (*visitor)(RefCounterVisitor& visitor, PtrBase const* ptr);
  void (*destroy)(PtrBase* self);
  RefTree (*decrement_graph)(PtrBase* self);
};

// PtrStorageBase* is used in general case, but PtrStorage<T>* in case there T
// is known
struct PtrStorageBase {
  PtrRefcounters ref_counters;
  std::uint16_t alloc_size;
  ManageTable const* manage_table;
};

template <typename T>
class PtrStorage {
 public:
  [[nodiscard]] auto* ptr() noexcept { return storage.ptr(); }
  [[nodiscard]] auto* ptr() const noexcept { return storage.ptr(); }

  PtrRefcounters ref_counters;
  std::uint16_t alloc_size;
  ManageTable const* manage_table;
  Storage<T> storage;
};

using PtrDnv =
    reflect::DomainNodeVisitor<RefCounterVisitor, reflect::VisitPolicy::kDeep>;

struct RefCounterVisitor {
  template <typename U>
  std::enable_if_t<ptr_management_internal::IsPtr<U>::value, bool> operator()(
      U const& obj) {
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

    auto& node = ref_tree.Emplace(RefTree::Value{obj_ptr, refs.main_refs, 0});
    node.value.reachable_ref_count++;

    assert((node.value.reachable_ref_count <= node.value.ref_count) &&
           "Bug in ref graph build algorithm");
    ref_tree.get(node_index).children_indices.push_back(node.index);

    auto next_visitor =
        RefCounterVisitor{cycle_detector, ref_tree, node.index, ignore_obj};
    obj.VisitDeeper(next_visitor);
    return false;
  }

  template <typename U>
  std::enable_if_t<!ptr_management_internal::IsPtr<U>::value> operator()(
      U const& /* obj */) {}

  template <typename U>
  void Visit(U* obj) {
    reflect::DomainVisit(cycle_detector, *obj, PtrDnv{*this});
  }

  reflect::CycleDetector& cycle_detector;
  RefTree& ref_tree;
  RefTree::Index node_index;
  void const* ignore_obj;
};

class PtrGraphBuilder {
 public:
  template <typename T>
  static RefTree BuildGraph(T& obj, PtrRefcounters const& ref_counters,
                            void const* ignore_obj) {
    auto ref_tree = RefTree{};
    auto& head_node =
        ref_tree.Emplace(RefTree::Value{&obj, ref_counters.main_refs, 0});
    head_node.value.reachable_ref_count++;

    auto cycle_detector = reflect::CycleDetector{};
    auto visitor = RefCounterVisitor{cycle_detector, ref_tree, head_node.index,
                                     ignore_obj};
    reflect::DomainVisit(cycle_detector, obj, PtrDnv{visitor});
    return ref_tree;
  }
};

}  // namespace ae

#endif  // AETHER_PTR_PTR_MANAGEMENT_H_
