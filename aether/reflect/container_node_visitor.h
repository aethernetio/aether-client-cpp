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

#ifndef AETHER_REFLECT_CONTAINER_NODE_VISITOR_H_
#define AETHER_REFLECT_CONTAINER_NODE_VISITOR_H_

#include <utility>
#include <type_traits>

#include "aether/type_traits.h"
#include "aether/reflect/node_visitor.h"
#include "aether/reflect/cycle_detector.h"

namespace ae::reflect {
namespace container_node_visitor_internal {
template <typename T, typename Enabled = void>
struct IsValueTypeIntegral : std::false_type {};

template <typename T>
struct IsValueTypeIntegral<
    T, std::enable_if_t<IsAssociatedContainer<T>::value &&
                        std::is_integral_v<typename T::mapped_type>>>
    : std::true_type {};

template <typename T>
struct IsValueTypeIntegral<
    T, std::enable_if_t<IsContainer<T>::value &&
                        std::is_integral_v<typename T::value_type>>>
    : std::true_type {};
}  // namespace container_node_visitor_internal

// Container types
// For non integral values
template <typename T>
struct NodeVisitor<
    T, std::enable_if_t<
           IsContainer<T>::value &&
           !container_node_visitor_internal::IsValueTypeIntegral<T>::value>> {
  using Policy = PolicyMatch<VisitPolicy::kDeep>;

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

  template <typename U, typename Visitor>
  void VisitImpl(U&& obj, CycleDetector& cycle_detector,
                 Visitor&& visitor) const {
    for (auto& v : obj) {
      ApplyVisit(v, cycle_detector, std::forward<Visitor>(visitor));
    }
  }

  template <typename U, typename Visitor>
  void ApplyVisit(U&& obj, CycleDetector& cycle_detector,
                  Visitor&& visitor) const {
    reflect::ApplyVisitor(std::forward<U>(obj), cycle_detector,
                          std::forward<Visitor>(visitor));
  }
};

// For integral values
template <typename T>
struct NodeVisitor<
    T, std::enable_if_t<
           IsContainer<T>::value &&
           container_node_visitor_internal::IsValueTypeIntegral<T>::value>> {
  using Policy = PolicyMatch<VisitPolicy::kDeep>;

  // for non const
  template <typename Visitor>
  void Visit(T& obj, CycleDetector& cycle_detector, Visitor&& visitor) const {
    reflect::ApplyVisitor(obj, cycle_detector, std::forward<Visitor>(visitor));
  }

  // for const
  template <typename Visitor>
  void Visit(T const& obj, CycleDetector& cycle_detector,
             Visitor&& visitor) const {
    reflect::ApplyVisitor(obj, cycle_detector, std::forward<Visitor>(visitor));
  }
};

// for pairs, used in map
template <typename K, typename V>
struct NodeVisitor<std::pair<K, V>> {
  using Policy = PolicyMatch<VisitPolicy::kDeep>;

  // for non const
  template <typename Visitor>
  void Visit(std::pair<K, V>& obj, CycleDetector& cycle_detector,
             Visitor&& visitor) const {
    // TODO: should we apply to the first ?
    // apply to the second
    ApplyVisit(obj.second, cycle_detector, std::forward<Visitor>(visitor));
  }

  // for const
  template <typename Visitor>
  void Visit(std::pair<K, V> const& obj, CycleDetector& cycle_detector,
             Visitor&& visitor) const {
    // TODO: should we apply to the first ?
    // apply to the second
    ApplyVisit(obj.second, cycle_detector, std::forward<Visitor>(visitor));
  }

  template <typename U, typename Visitor>
  void ApplyVisit(U&& obj, CycleDetector& cycle_detector,
                  Visitor&& visitor) const {
    reflect::ApplyVisitor(std::forward<U>(obj), cycle_detector,
                          std::forward<Visitor>(visitor));
  }
};

}  // namespace ae::reflect

#endif  // AETHER_REFLECT_CONTAINER_NODE_VISITOR_H_
