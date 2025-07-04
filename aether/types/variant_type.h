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
#include <utility>
#include <variant>

#include "aether/mstream.h"

namespace ae {
template <typename IndexType, typename... Ts>
class VariantType : public std::variant<Ts...> {
 public:
  using index_type = IndexType;

  // use all variant constructors
  using std::variant<Ts...>::variant;
  virtual ~VariantType() = default;

 private:
  template <std::size_t I, typename Stream>
  static bool LoadElement(Stream &stream, std::variant<Ts...> &var) {
    using T = std::variant_alternative_t<I, std::variant<Ts...>>;
    T t{};
    stream >> t;
    var = std::move(t);
    return true;
  }

  template <typename Stream, std::size_t... Is>
  static void Load(Stream &stream, std::size_t order, std::variant<Ts...> &var,
                   std::index_sequence<Is...> const &) {
    (((order == Is) ? LoadElement<Is>(stream, var) : false) || ...);
  }

  template <typename Stream, std::size_t... Is>
  static void Save(Stream &stream, std::size_t order,
                   std::variant<Ts...> const &var,
                   std::index_sequence<Is...> const &) {
    (((order == Is) ? stream << std::get<Is>(var) : stream), ...);
  }

  template <typename Type, std::size_t I, std::size_t... Is>
  auto const &GetImpl(std::index_sequence<I, Is...> const &) const {
    if constexpr (std::is_same_v<Type, std::variant_alternative_t<
                                           I, std::variant<Ts...>>>) {
      return std::get<I>(*this);
    } else {
      return GetImpl<Type>(std::index_sequence<Is...>{});
    }
  }

 public:
  // Get currently stored variant index
  auto Index() const { return OrderToIndex(this->index()); }

  template <typename Type>
  auto const &Get() const {
    static_assert((std::is_same_v<Type, Ts> || ...), "Type not found");
    return GetImpl<Type>(std::make_index_sequence<sizeof...(Ts)>());
  }

  // Conversion from index type to variant type order
  virtual std::size_t IndexToOrder(index_type index) const {
    return static_cast<std::size_t>(index);
  }

  // Conversion from variant type order to index type
  virtual index_type OrderToIndex(std::size_t order) const {
    return static_cast<index_type>(order);
  }

  template <typename Ib>
  friend imstream<Ib> operator>>(imstream<Ib> &is, VariantType &v) {
    index_type index{};
    is >> index;
    auto order = v.IndexToOrder(index);
    Load(is, order, v, std::make_index_sequence<sizeof...(Ts)>());
    return is;
  }

  template <typename Ob>
  friend omstream<Ob> operator<<(omstream<Ob> &os, VariantType const &v) {
    auto order = v.index();
    os << v.OrderToIndex(order);
    Save(os, order, v, std::make_index_sequence<sizeof...(Ts)>());
    return os;
  }
};
}  // namespace ae

#endif  // AETHER_TYPES_VARIANT_TYPE_H_
