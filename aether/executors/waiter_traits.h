/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_EXECUTORS_WAITER_TRAITS_H_
#define AETHER_EXECUTORS_WAITER_TRAITS_H_

#include <tuple>
#include <variant>
#include <type_traits>

#include "aether/warning_disable.h"

// IWYU pragma: begin_exports
DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include <stdexec/execution.hpp>
DISABLE_WARNING_POP()

#include "aether/meta/ignore_t.h"
#include "aether/meta/type_list.h"

namespace ae::ex {
template <template <typename> typename Filter, typename T>
struct FilterList;

template <template <typename> typename Filter, typename T, typename... Ts>
struct FilterList<Filter, TypeList<T, Ts...>> {
  using type = JoinedTypeList_t<
      std::conditional_t<!Filter<T>::value, TypeList<T>, TypeList<>>,
      typename FilterList<Filter, TypeList<Ts...>>::type>;
};
template <template <typename> typename Filter>
struct FilterList<Filter, TypeList<>> {
  using type = TypeList<>;
};

template <typename T>
struct FilterExceptionPtr {
  static constexpr bool value =
      std::is_same_v<std::decay_t<T>, std::exception_ptr>;
};

/**
 * Expand value list.
 * It may be a variant of values
 * Values may be a tuples or single values
 * Or it may be a single value or void
 */
template <typename T>
struct ExpandTuple {
  using type = T;
};
template <>
struct ExpandTuple<TypeList<>> {
  using type = Ignore;
};
template <typename T>
struct ExpandTuple<TypeList<T>> {
  using type = T;
};
template <typename... T>
struct ExpandTuple<TypeList<T...>> {
  using type = std::tuple<T...>;
};

template <typename>
struct ExpandVariant;
template <>
struct ExpandVariant<TypeList<>> {
  using type = Ignore;
};
template <typename T>
struct ExpandVariant<TypeList<T>> {
  using type = ExpandTuple<T>::type;
};
template <typename... T>
struct ExpandVariant<TypeList<T...>> {
  using type = TypeListToTemplate_t<
      std::variant,
      typename FilterList<IsIgnore,
                          TypeList<typename ExpandTuple<T>::type...>>::type>;
};

template <typename Completions>
struct CompletionsTraitsImpl {
  using ValueList =
      stdexec::__value_types_t<Completions, stdexec::__q<TypeList>,
                               stdexec::__q<TypeList>>;
  using ErrorList =
      stdexec::__error_types_t<Completions, stdexec::__q<TypeList>,
                               stdexec::__q<stdexec::__msingle>>;

  using FilteredErrorList = FilterList<FilterExceptionPtr, ErrorList>::type;

  using ValueType = ExpandVariant<ValueList>::type;
  using ErrorType = ExpandVariant<FilteredErrorList>::type;
};

template <typename... Sigs>
struct CompletionsTraits
    : CompletionsTraitsImpl<stdexec::completion_signatures<Sigs...>> {};

}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_WAITER_TRAITS_H_
