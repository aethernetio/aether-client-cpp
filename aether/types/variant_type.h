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

#ifndef AETHER_TYPES_VARIANT_TYPE_H_
#define AETHER_TYPES_VARIANT_TYPE_H_

#include <cstddef>
#include <cassert>
#include <utility>
#include <variant>
#include <functional>

#include "aether/mstream.h"
#include "aether/types/type_list.h"

namespace ae {
template <auto I, typename T>
struct VPair {
  constexpr static auto Index = I;
  using Type = T;
};

/**
 * \brief Variant type with serialization.
 * All types mapped to indexes.
 */
template <typename IndexType, typename... Variants>
class VariantType : public std::variant<typename Variants::Type...> {
 public:
  using index_type = IndexType;
  using Variant = std::variant<typename Variants::Type...>;

  // use all variant constructors
  using Variant::Variant;
  using Variant::operator=;

 private:
  template <std::size_t... Is>
  static constexpr index_type GetIndexByOrder(std::size_t order,
                                              std::index_sequence<Is...>) {
    index_type res{};
    (
        [&]() {
          if (order == Is) {
            res = TypeAtT<Is, TypeList<Variants...>>::Index;
          }
        }(),
        ...);
    return res;
  }

  template <std::size_t... Is>
  static constexpr std::size_t GetOrderByIndex(index_type index,
                                               std::index_sequence<Is...>) {
    std::size_t res{};
    (
        [&]() {
          if (index == TypeAtT<Is, TypeList<Variants...>>::Index) {
            res = Is;
          }
        }(),
        ...);
    return res;
  }

  template <std::size_t I, typename Stream>
  static bool LoadElement(Stream &stream, Variant &var) {
    using T = std::variant_alternative_t<I, Variant>;
    T t{};
    stream >> t;
    var = std::move(t);
    return true;
  }

  template <typename Stream, std::size_t... Is>
  static void Load(Stream &stream, std::size_t order, Variant &var,
                   std::index_sequence<Is...> const &) {
    (std::invoke([&]() {
       if (order == Is) {
         LoadElement<Is>(stream, var);
       }
     }),
     ...);
  }

  template <typename Stream, std::size_t... Is>
  static void Save(Stream &stream, std::size_t order, Variant const &var,
                   std::index_sequence<Is...> const &) {
    (std::invoke([&]() {
       if (order == Is) {
         stream << std::get<Is>(var);
       }
     }),
     ...);
  }

  template <typename Type, std::size_t I, std::size_t... Is>
  constexpr auto const &GetImpl(std::index_sequence<I, Is...> const &) const {
    if constexpr (std::is_same_v<Type,
                                 std::variant_alternative_t<I, Variant>>) {
      return std::get<I>(*this);
    } else {
      return GetImpl<Type>(std::index_sequence<Is...>{});
    }
  }

 public:
  // Get currently stored variant index
  constexpr auto Index() const {
    return GetIndexByOrder(this->index(),
                           std::make_index_sequence<sizeof...(Variants)>());
  }

  template <typename Type>
  constexpr auto const &Get() const {
    static_assert((std::is_same_v<Type, typename Variants::Type> || ...),
                  "Type not found");
    return GetImpl<Type>(std::make_index_sequence<sizeof...(Variants)>());
  }

  template <typename Ib>
  friend imstream<Ib> operator>>(imstream<Ib> &is, VariantType &v) {
    index_type index{};
    is >> index;
    auto order =
        GetOrderByIndex(index, std::make_index_sequence<sizeof...(Variants)>());
    assert(order < sizeof...(Variants));
    Load(is, order, v, std::make_index_sequence<sizeof...(Variants)>());
    return is;
  }

  template <typename Ob>
  friend omstream<Ob> operator<<(omstream<Ob> &os, VariantType const &v) {
    auto order = v.index();
    os << GetIndexByOrder(order,
                          std::make_index_sequence<sizeof...(Variants)>());
    Save(os, order, v, std::make_index_sequence<sizeof...(Variants)>());
    return os;
  }
};
}  // namespace ae

#endif  // AETHER_TYPES_VARIANT_TYPE_H_
