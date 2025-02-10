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

#ifndef AETHER_OBJ_DOMAIN_TREE_H_
#define AETHER_OBJ_DOMAIN_TREE_H_

#include <type_traits>
#include <set>
#include <utility>

#include "aether/obj/type_index.h"
#include "aether/obj/visitor_policies.h"

namespace ae {

struct CycleDetector {
  struct ObjEntry {
    bool operator<(ObjEntry const& rhs) const {
      auto lhs_ptr = reinterpret_cast<std::ptrdiff_t>(ptr);
      auto rhs_ptr = reinterpret_cast<std::ptrdiff_t>(rhs.ptr);
      return (static_cast<std::ptrdiff_t>(type_index) + lhs_ptr) <
             (static_cast<std::ptrdiff_t>(rhs.type_index) + rhs_ptr);
    }

    std::uint32_t type_index;
    void const* ptr;
  };

  std::set<ObjEntry> visited_objects_;

  template <typename T>
  void Add(T const* ptr) {
    visited_objects_.insert(ObjEntry{GetTypeIndex<T>(), ptr});
  }

  template <typename T>
  bool IsVisited(T const* ptr) const {
    auto e = ObjEntry{GetTypeIndex<T>(), ptr};
    return visited_objects_.find(e) != visited_objects_.end();
  }
};

template <typename VisitPolicy, typename Visitor>
class DomainNodeVisitor {
 public:
  DomainNodeVisitor(Visitor visitor, CycleDetector& cycle_detector)
      : visitor{std::forward<Visitor>(visitor)},
        cycle_detector{cycle_detector} {}

  template <typename... Ts>
  void operator()(Ts&... ts) {
    (VisitNode(ts), ...);
  }

  template <typename T>
  void VisitNode(T& t) {
    if constexpr (VisitPolicy::template VisitorApplicable<T, Visitor>::value) {
      auto propagate = VisitPolicy::template ApplyVisit<>(t, visitor);
      if (!propagate) {
        return;
      }
    }
    if constexpr (VisitPolicy::template HasVisitMethod<
                      T, DomainNodeVisitor>::value) {
      VisitPolicy::template Visit<T>(*this, t);
    }
  }

  Visitor visitor;
  CycleDetector& cycle_detector;
};

template <typename VisitPolicy = ExplicitVisitPolicy>
class DomainTree {
 public:
  template <typename T, typename Visitor>
  static void Visit(T& t, Visitor&& visitor) {
    CycleDetector cd;
    Visit(cd, t, std::forward<Visitor>(visitor));
  }

  template <typename T, typename Visitor>
  static void Visit(CycleDetector& cycle_detector, T& t, Visitor&& visitor) {
    using Dnv = DomainNodeVisitor<VisitPolicy, Visitor>;
    if constexpr (VisitPolicy::template HasVisitMethod<std::decay_t<T>,
                                                       Dnv>::value) {
      auto dnv = Dnv{std::forward<Visitor>(visitor), cycle_detector};
      VisitPolicy::template Visit<T>(dnv, t);
    }
  }
};
}  // namespace ae

#endif  // AETHER_OBJ_DOMAIN_TREE_H_
