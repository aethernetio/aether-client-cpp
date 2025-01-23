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

#ifndef AETHER_OBJ_VISITOR_POLICIES_H_
#define AETHER_OBJ_VISITOR_POLICIES_H_

#include <type_traits>
#include "aether/type_traits.h"

namespace ae {

// Check if T has regular template<typename Dnv> void Visit(Dnv& dnv) method
template <typename T, typename Dnv, typename _ = void>
struct HasVisitMethod : std::false_type {};

template <typename T, typename Dnv>
struct HasVisitMethod<
    T, Dnv,
    std::void_t<decltype(std::declval<T>().Visit(std::declval<Dnv&>()))>>
    : std::true_type {};

template <typename T, typename Visitor, typename _ = void>
struct VisitorApplicable : std::false_type {};

template <typename T, typename Visitor>
struct VisitorApplicable<T, Visitor,
                         std::enable_if_t<std::is_invocable_v<Visitor, T&>>>
    : std::true_type {};

// Common logic to apply visitor for any T
struct VisitPolicyCommon {
  template <typename T, typename Visitor>
  inline constexpr static bool ApplyVisit(T& t, Visitor& visitor) {
    if constexpr (VisitorApplicable<T, Visitor>::value) {
      if constexpr (std::is_convertible_v<std::invoke_result_t<Visitor, T&>,
                                          bool>) {
        return visitor(t);
      } else {
        visitor(t);
        return true;  // propagate by default
      }
    } else {
      return true;  // propagate by default
    }
  }
};

// Call Visit method for T if it exists
struct ExplicitVisitPolicy : public VisitPolicyCommon {
  template <typename T, typename Dnv>
  using HasVisitMethod = HasVisitMethod<T, Dnv>;

  template <typename T, typename Visitor>
  using VisitorApplicable = VisitorApplicable<T, Visitor>;

  template <typename T, typename Dnv>
  inline static void Visit(Dnv& dnv, T& t) {
    if constexpr (HasVisitMethod<std::decay_t<T>, Dnv>::value) {
      if (!dnv.cycle_detector.IsVisited(&t)) {
        dnv.cycle_detector.Add(&t);
        t.Visit(dnv);
      }
    }
  }
};

// apply VisitPolicy for T if it exists or dive deeper in Container<T>
template <typename VisitPolicy = ExplicitVisitPolicy>
struct DeepVisitPolicy {
  template <typename T, typename Dnv, typename _ = void>
  struct HasVisitMethod : std::false_type {};

  template <typename T, typename Dnv>
  struct HasVisitMethod<
      T, Dnv,
      std::enable_if_t<VisitPolicy::template HasVisitMethod<T, Dnv>::value ||
                       VisitPolicy::template HasVisitMethod<
                           std::remove_pointer_t<T>, Dnv>::value>>
      : std::true_type {};

  template <typename T, typename Dnv>
  struct HasVisitMethod<
      T, Dnv,
      std::enable_if_t<
          IsContainer<T>::value &&
          (VisitPolicy::template HasVisitMethod<typename T::value_type,
                                                Dnv>::value ||
           VisitPolicy::template HasVisitMethod<
               std::remove_pointer_t<typename T::value_type>, Dnv>::value)>>
      : std::true_type {};

  template <typename T, typename Dnv>
  struct HasVisitMethod<
      T, Dnv,
      std::enable_if_t<
          IsAssociatedContainer<T>::value &&
          (VisitPolicy::template HasVisitMethod<typename T::mapped_type,
                                                Dnv>::value ||
           VisitPolicy::template HasVisitMethod<
               std::remove_pointer_t<typename T::mapped_type>, Dnv>::value)>>
      : std::true_type {};

  template <typename T, typename Visitor, typename _ = void>
  struct VisitorApplicable : std::false_type {};

  template <typename T, typename Visitor>
  struct VisitorApplicable<
      T, Visitor,
      std::enable_if_t<IsContainer<T>::value &&
                       !IsAssociatedContainer<T>::value &&
                       std::is_invocable_v<Visitor, typename T::value_type>>>
      : std::true_type {};

  template <typename T, typename Visitor>
  struct VisitorApplicable<
      T, Visitor,
      std::enable_if_t<IsAssociatedContainer<T>::value &&
                       std::is_invocable_v<Visitor, typename T::mapped_type>>>
      : std::true_type {};

  template <typename T, typename Visitor>
  struct VisitorApplicable<T, Visitor,
                           std::enable_if_t<!IsAssociatedContainer<T>::value &&
                                            !IsContainer<T>::value &&
                                            std::is_invocable_v<Visitor, T&>>>
      : std::true_type {};

  template <typename T, typename Visitor>
  inline static bool ApplyVisit(T& t, Visitor& visitor) {
    return ApplyDeeper(t, visitor);
  }

  template <typename T, typename Dnv>
  inline static void Visit(Dnv& dnv, T& t) {
    VisitDeeper(dnv, t);
  }

 private:
  template <typename T, typename Visitor>
  inline static std::enable_if_t<
      !IsAssociatedContainer<std::decay_t<T>>::value &&
          !IsContainer<std::decay_t<T>>::value,
      bool>
  ApplyDeeper(T& t, Visitor& visitor) {
    return VisitPolicy::template ApplyVisit<>(t, visitor);
  }

  template <typename T, typename Visitor>
  inline static std::enable_if_t<
      !IsAssociatedContainer<std::decay_t<T>>::value &&
          !IsContainer<std::decay_t<T>>::value,
      bool>
  ApplyDeeper(T* t, Visitor& visitor) {
    return VisitPolicy::template ApplyVisit<>(t, visitor);
  }

  template <typename T, typename Visitor>
  inline static std::enable_if_t<
      IsAssociatedContainer<std::decay_t<T>>::value ||
          IsContainer<std::decay_t<T>>::value,
      bool>
  ApplyDeeper(T& t, Visitor& visitor) {
    using TType = std::decay_t<T>;
    bool propagate = false;
    if constexpr (IsAssociatedContainer<TType>::value) {
      for (auto& [_, v] : t) {
        auto p = ApplyDeeper(v, visitor);
        propagate = p || propagate;
      }
    } else if (IsContainer<TType>::value) {
      for (auto& v : t) {
        auto p = ApplyDeeper(v, visitor);
        propagate = p || propagate;
      }
    }

    return propagate;
  }

  template <typename T, typename Dnv>
  inline static std::enable_if_t<
      VisitPolicy::template HasVisitMethod<std::decay_t<T>, Dnv>::value>
  VisitDeeper(Dnv& dnv, T& t) {
    VisitPolicy::template Visit<T>(dnv, t);
  }

  template <typename T, typename Dnv>
  inline static std::enable_if_t<
      VisitPolicy::template HasVisitMethod<std::decay_t<T>, Dnv>::value>
  VisitDeeper(Dnv& dnv, T* t) {
    VisitPolicy::template Visit<T>(dnv, *t);
  }

  template <typename T, typename Dnv>
  inline static std::enable_if_t<IsContainer<T>::value> VisitDeeper(Dnv& dnv,
                                                                    T& t) {
    if constexpr (IsAssociatedContainer<T>::value) {
      // for associated containers, visit the values
      if constexpr (VisitPolicy::template HasVisitMethod<
                        typename T::mapped_type, Dnv>::value ||
                    VisitPolicy::template HasVisitMethod<
                        std::remove_pointer_t<typename T::mapped_type>,
                        Dnv>::value) {
        for (auto& [_, v] : t) {
          VisitDeeper(dnv, v);
        }
      }
    } else {
      if constexpr (VisitPolicy::template HasVisitMethod<typename T::value_type,
                                                         Dnv>::value ||
                    VisitPolicy::template HasVisitMethod<
                        std::remove_pointer_t<typename T::value_type>,
                        Dnv>::value) {
        for (auto& v : t) {
          VisitDeeper(dnv, v);
        }
      }
    }
  }

  // default case
  template <typename T, typename Dnv>
  inline static std::enable_if_t<
      !VisitPolicy::template HasVisitMethod<T, Dnv>::value &&
      !IsContainer<T>::value>
  VisitDeeper(Dnv& /* dnv */, T& /* t */) {}
};

// Call Visit method for T by one of applicable VisitPolicy
template <typename... TVisitPolicies>
struct MultiplexVisitorPolicy {
  template <typename T, typename Dnv>
  struct HasVisitMethod {
    static constexpr bool value =
        (TVisitPolicies::template HasVisitMethod<T, Dnv>::value || ...);
  };

  template <typename T, typename Visitor>
  struct VisitorApplicable {
    static constexpr bool value =
        (TVisitPolicies::template VisitorApplicable<T, Visitor>::value || ...);
  };

  template <typename T, typename Visitor>
  static constexpr bool ApplyVisit(T& t, Visitor& visitor) {
    return ApplyVisitImpl<TVisitPolicies...>(t, visitor);
  }

  template <typename T, typename Dnv>
  static void Visit(Dnv& dnv, T& t) {
    // visit by first applicable policy
    VisitImpl<TVisitPolicies...>(dnv, t);
  }

 private:
  template <typename TVisitPolicy, typename... TVPs, typename T,
            typename Visitor>
  static constexpr bool ApplyVisitImpl(T& t, Visitor& visitor) {
    // apply visit by first applicable policy
    if constexpr (TVisitPolicy::template VisitorApplicable<T, Visitor>::value) {
      return TVisitPolicy::template ApplyVisit<>(t, visitor);
    } else {
      // if not applied, try next policy
      if constexpr (sizeof...(TVPs) > 0) {
        return ApplyVisitImpl<TVPs...>(t, visitor);
      }
      return true;
    }
  }

  template <typename TVisitPolicy, typename... TVPs, typename T, typename Dnv>
  static void VisitImpl(Dnv& dnv, T& t) {
    if constexpr (TVisitPolicy::template HasVisitMethod<T, Dnv>::value) {
      // visited
      return TVisitPolicy::template Visit<T>(dnv, t);
    } else {
      if constexpr (sizeof...(TVPs) > 0) {
        return VisitImpl<TVPs...>(dnv, t);
      }
    }
  }
};
}  // namespace ae

#endif  // AETHER_OBJ_VISITOR_POLICIES_H_
