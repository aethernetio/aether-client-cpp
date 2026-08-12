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

#include <cassert>
#include <cstdint>
#include <functional>
#include <type_traits>

#include "aether-miscpp/domain_visitor/domain_visitor.h"
#include "aether-miscpp/types/aligned_storage.h"

namespace ae {
struct PtrRefcounters {
  std::uint16_t main_refs = 0;
  std::uint16_t weak_refs = 0;
};

class PtrBase;

// function table for managing Ptr<T> from PtrBase
struct ManageTable {
  void (*destroy)(PtrBase* self);
  void (*visit_children)(PtrBase const* self, void* arg,
                         void (*cb)(void* arg, PtrBase const* child));
};

// PtrStorageBase* is used in general case, but PtrStorage<T>* in case there T
// is known
struct PtrStorageBase {
  PtrRefcounters ref_counters;
  std::size_t alloc_size;
  ManageTable const* manage_table;
};

template <typename T>
class PtrStorage {
 public:
  [[nodiscard]] auto* ptr() noexcept { return storage.ptr(); }
  [[nodiscard]] auto* ptr() const noexcept { return storage.ptr(); }

  PtrRefcounters ref_counters;
  std::size_t alloc_size;
  ManageTable const* manage_table;
  Storage<T> storage;
};

template <typename T>
class Ptr;

namespace ptr_management_internal {
template <typename T, typename _ = void>
struct IsPtr : std::false_type {};

template <typename T, template <typename...> typename UPtr>
struct IsPtr<UPtr<T>, std::enable_if_t<std::is_base_of_v<Ptr<T>, UPtr<T>>>>
    : std::true_type {};
}  // namespace ptr_management_internal

template <typename T>
static constexpr inline auto IsPtr_v = ptr_management_internal::IsPtr<T>::value;

struct RefVisitor {
  template <typename U>
    requires(IsPtr_v<U>)
  bool operator()(U const& obj) {
    // always return false to prevent go into the obj itself

    auto const* obj_ptr = static_cast<void const*>(obj.get());
    if (obj_ptr == nullptr) {
      return false;
    }

    std::invoke(cb, arg, &obj);
    return false;
  }

  template <typename U>
    requires(!IsPtr_v<U>)
  void operator()(U const& /* obj */) {}

  void* arg;
  void (*cb)(void* arg, PtrBase const* child);
};

using PtrRefDnv =
    domain_visitor::DomainNodeVisitor<RefVisitor&,
                                      domain_visitor::VisitPolicy::kAny>;
}  // namespace ae

#endif  // AETHER_PTR_PTR_MANAGEMENT_H_
