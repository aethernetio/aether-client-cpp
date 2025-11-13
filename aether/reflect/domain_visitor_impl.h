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

#ifndef AETHER_REFLECT_DOMAIN_VISITOR_IMPL_H_
#define AETHER_REFLECT_DOMAIN_VISITOR_IMPL_H_

#include <utility>
#include <type_traits>

#include "aether/reflect/node_visitor.h"
#include "aether/reflect/cycle_detector.h"

namespace ae::reflect {

template <typename Visitor, int policy>
struct DomainNodeVisitor {
  static constexpr auto kPolicy = policy;

  explicit constexpr DomainNodeVisitor(Visitor vis)
      : visitor{std::forward<Visitor>(vis)} {}

  template <typename U>
  auto operator()(U&& val) {
    return std::invoke(std::forward<Visitor>(visitor), std::forward<U>(val));
  }

  Visitor visitor;
};

template <typename T>
struct IsDomainNodeVisitor : std::false_type {};

template <typename V, int policy>
struct IsDomainNodeVisitor<DomainNodeVisitor<V, policy>> : std::true_type {};

template <typename T, typename Visitor, int policy = VisitPolicy::kShallow,
          std ::enable_if_t<!IsDomainNodeVisitor<Visitor>::value, int> = 0>
void DomainVisit(T&& obj, Visitor&& visitor) {
  auto cycle_detector = CycleDetector{};
  DomainVisit(
      cycle_detector, std::forward<T>(obj),
      DomainNodeVisitor<Visitor, policy>{std::forward<Visitor>(visitor)});
}

template <typename T, typename Visitor, int policy = VisitPolicy::kShallow>
void DomainVisit(T&& obj, DomainNodeVisitor<Visitor, policy> visitor) {
  auto cycle_detector = CycleDetector{};
  DomainVisit(cycle_detector, std::forward<T>(obj), std::move(visitor));
}

template <typename T, typename Visitor, int policy = VisitPolicy::kShallow>
void DomainVisit(CycleDetector& cycle_detector, T&& obj,
                 DomainNodeVisitor<Visitor, policy>&& visitor) {
  if constexpr (HasNodeVisitor<std::decay_t<T>>::value) {
    using Nv = NodeVisitor<std::decay_t<T>>;
    if constexpr (Nv::Policy::Match(policy)) {
      if (!cycle_detector.Add(&obj)) {
        return;
      }
      auto node_visitor = Nv{};
      std::as_const(node_visitor)
          .Visit(std::forward<T>(obj), cycle_detector, std::move(visitor));
    }
  }
}

}  // namespace ae::reflect

#endif  // AETHER_REFLECT_DOMAIN_VISITOR_IMPL_H_
