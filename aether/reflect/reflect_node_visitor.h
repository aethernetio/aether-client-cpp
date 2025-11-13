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

#ifndef AETHER_REFLECT_REFLECT_NODE_VISITOR_H_
#define AETHER_REFLECT_REFLECT_NODE_VISITOR_H_

#include <utility>

#include "aether/reflect/reflect_impl.h"
#include "aether/reflect/node_visitor.h"
#include "aether/reflect/cycle_detector.h"

namespace ae::reflect {
// For reflectable classes
template <typename T>
struct NodeVisitor<T, std::enable_if_t<IsReflectable<T>::value>> {
  using Policy = AnyPolicyMatch;

  // for non const
  template <typename Visitor>
  void Visit(T& obj, CycleDetector& cycle_detector, Visitor&& visitor) const {
    VisitImpl(obj, cycle_detector, std::forward<Visitor>(visitor));
  }

  // for const
  template <typename Visitor>
  void Visit(T const& obj, CycleDetector& cycle_detector,
             Visitor&& visitor) const {
    VisitImpl(obj, cycle_detector, std::forward<Visitor>(visitor));
  }

  // universal
  template <typename U, typename Visitor>
  void VisitImpl(U&& obj, CycleDetector& cycle_detector,
                 Visitor&& visitor) const {
    auto reflection = Reflection{std::forward<U>(obj)};
    reflection.Apply([&](auto&... fields) {
      (ApplyVisit(fields, cycle_detector, std::forward<Visitor>(visitor)), ...);
    });
  }

  template <typename U, typename Visitor>
  void ApplyVisit(U&& obj, CycleDetector& cycle_detector,
                  Visitor&& visitor) const {
    reflect::ApplyVisitor(std::forward<U>(obj), cycle_detector,
                          std::forward<Visitor>(visitor));
  }
};
}  // namespace ae::reflect

#endif  // AETHER_REFLECT_REFLECT_NODE_VISITOR_H_
