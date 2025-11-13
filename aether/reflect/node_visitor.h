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

#ifndef AETHER_REFLECT_NODE_VISITOR_H_
#define AETHER_REFLECT_NODE_VISITOR_H_

#include <utility>
#include <type_traits>
#include <functional>

#include "aether/reflect/cycle_detector.h"

namespace ae::reflect {
enum VisitPolicy {
  kNone = 0,
  kShallow = 1,          // go only by first levels
  kReflection = 1 << 1,  // go deeper by reflections
  kDeep = 1 << 2,        // go deep as possible
  kAny = 0xfffffff,
};

template <VisitPolicy policy>
struct PolicyMatch {
  static constexpr bool Match(int val) { return (val & policy) > 0; }
};

template <>
struct PolicyMatch<VisitPolicy::kAny> {
  static constexpr bool Match(int /* val */) { return true; }
};

using AnyPolicyMatch = PolicyMatch<VisitPolicy::kAny>;

template <typename T, typename Enable = void>
struct NodeVisitor;

/**
 * \brief Call visitor with obj.
 * If Visitor returns bool, propagate further visiting if true.
 */
template <typename U, typename Visitor>
bool CallVisitor(U&& obj, Visitor&& visitor) {
  if constexpr (std::is_same_v<bool, std::invoke_result_t<Visitor, U>>) {
    // do not propagate on false
    return std::invoke(std::forward<Visitor>(visitor), std::forward<U>(obj));
  } else {
    std::invoke(std::forward<Visitor>(visitor), std::forward<U>(obj));
    return true;
  }
}

template <typename T, typename Visitor>
void DomainVisit(CycleDetector& cycle_detector, T&& obj, Visitor&& visitor);

template <typename U, typename Visitor>
void ApplyVisitor(U&& obj, CycleDetector& cycle_detector, Visitor&& visitor) {
  if constexpr ((Visitor::kPolicy != VisitPolicy::kAny) &&
                PolicyMatch<VisitPolicy::kShallow>::Match(Visitor::kPolicy)) {
    // Call visitor only on current node
    CallVisitor(std::forward<U>(obj), std::forward<Visitor>(visitor));
  } else {
    // Call visitor on current node and go deeper with new DomainVisit
    if (CallVisitor(std::forward<U>(obj), std::forward<Visitor>(visitor))) {
      DomainVisit(cycle_detector, std::forward<U>(obj),
                  std::forward<Visitor>(visitor));
    }
  }
}

template <typename T, typename Enable = void>
struct HasNodeVisitor : std::false_type {};

template <typename T>
struct HasNodeVisitor<T, std::void_t<decltype(NodeVisitor<T>{})>>
    : std::true_type {};

}  // namespace ae::reflect

#endif  // AETHER_REFLECT_NODE_VISITOR_H_
