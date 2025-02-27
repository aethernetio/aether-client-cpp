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

#ifndef AETHER_REFLECT_REFLECT_H_
#define AETHER_REFLECT_REFLECT_H_

#include <tuple>
#include <utility>
#include <type_traits>

namespace ae::reflect {
namespace reflect_internal {
template <typename U1>
static constexpr bool CheckDuplicateImpl(U1 /*check1 */) {
  return false;
}

static constexpr bool CheckDuplicateImpl() { return false; }

template <typename U1, typename U2, typename... U>
static constexpr bool CheckDuplicateImpl(U1 check1, U2 check2, U... checks) {
  if constexpr (std::is_same_v<U1, U2>) {
    if (check1 == check2) {
      return true;
    }
  } else {
    return CheckDuplicateImpl(check2, checks...);
  }
  return false;
}

template <typename... U>
static constexpr bool CheckDuplicate(U... checks) {
  return CheckDuplicateImpl(checks...);
}
}  // namespace reflect_internal

/**
 * \brief class T's member fields list
 */
template <typename... TFields>
struct FieldList {
  using FieldsList = std::tuple<TFields...>;
  static constexpr std::size_t kSize = sizeof...(TFields);

  explicit constexpr FieldList(TFields... fields) : fields_list_{fields...} {}

  /**
   * \brief Apply func to obj fields
   */
  template <typename Func>
  constexpr void Apply(Func&& func) {
    // apply to each field
    std::apply(std::forward<Func>(func), fields_list_);
  }

  /**
   * \brief Get field
   */
  template <std::size_t I>
  constexpr auto& get() {
    return std::get<I>(fields_list_);
  }

 private:
  FieldsList fields_list_;
};

template <typename... U>
FieldList(U&&...) -> FieldList<U...>;

template <typename T, typename Enable = void>
class Reflection;

template <typename T>
class Reflection<T, std::void_t<decltype(std::declval<T&>().FieldList())>> {
  using FieldList = decltype(std::declval<T&>().FieldList());

 public:
  constexpr explicit Reflection(T obj) : field_list_{obj.FieldList()} {}

  template <typename Func>
  constexpr void Apply(Func&& func) {
    field_list_.Apply(std::forward<Func>(func));
  }

  template <std::size_t I>
  constexpr auto& get() {
    return field_list_.template get<I>();
  }

  constexpr std::size_t size() const { return FieldList::kSize; }

 private:
  FieldList field_list_;
};

template <typename T>
Reflection(T&&) -> Reflection<T>;

template <typename T, typename Enable = void>
struct IsReflectable : std::false_type {};

template <typename T>
struct IsReflectable<T,
                     std::void_t<decltype(Reflection<T&>{std::declval<T&>()})>>
    : std::true_type {};

}  // namespace ae::reflect

#define AE_CLASS_REFLECT(...)                                                  \
  constexpr auto FieldList() { return ::ae::reflect::FieldList{__VA_ARGS__}; } \
  constexpr auto FieldList() const {                                           \
    return ::ae::reflect::FieldList{__VA_ARGS__};                              \
  }

#endif  // AETHER_REFLECT_REFLECT_H_
