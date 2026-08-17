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

#include <cassert>
#include <cstddef>
#include <utility>
#include <variant>

#include "aether-miscpp/meta/type_list.h"
#include "aether-miscpp/serialization/serialization.h"

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
  static constexpr std::size_t kSize = sizeof...(Variants);

  // use all variant constructors
  using Variant::Variant;
  using Variant::operator=;

  // Get currently stored variant index
  constexpr auto Index() const {
    return GetIndexByOrder(this->index(),
                           std::make_index_sequence<sizeof...(Variants)>());
  }

  template <typename Type>
  constexpr auto const& Get() const {
    static_assert((std::is_same_v<Type, typename Variants::Type> || ...),
                  "Type not found");
    return GetImpl<Type>(std::make_index_sequence<sizeof...(Variants)>());
  }

  template <std::size_t... Is>
  static constexpr IndexType GetIndexByOrder(std::size_t order,
                                             std::index_sequence<Is...>) {
    constexpr auto index_arr =
        std::array{TypeAt_t<Is, TypeList<Variants...>>::Index...};
    return index_arr[order];
  }

  template <std::size_t... Is>
  static constexpr std::size_t GetOrderByIndex(IndexType index,
                                               std::index_sequence<Is...>) {
    std::size_t res{};
    bool found = (((TypeAt_t<Is, TypeList<Variants...>>::Index == index)
                       ? (res = Is, true)
                       : false) ||
                  ...);
    if (found) {
      return res;
    }
    return kSize;
  }

  template <typename Type, std::size_t I, std::size_t... Is>
  constexpr auto const& GetImpl(std::index_sequence<I, Is...> const&) const {
    if constexpr (std::is_same_v<Type,
                                 std::variant_alternative_t<I, Variant>>) {
      return std::get<I>(*this);
    } else {
      return GetImpl<Type>(std::index_sequence<Is...>{});
    }
  }

  // serialization
  template <std::size_t I = 0>
  static seri::SeriResult Load(seri::Archive auto& archive, std::size_t order,
                               Variant& val) {
    if constexpr (I >= kSize) {
      return Error{seri::invalid_variant_index};
    } else {
      if (I == order) {
        auto& ref = val.template emplace<I>();
        TRY_RESULT(archive.Load(ref));
        return Ok{seri::good};
      }
      return Load<I + 1>(archive, order, val);
    }
  }

  template <std::size_t I = 0>
  static seri::SeriResult Save(seri::Archive auto& archive, std::size_t order,
                               Variant const& val) {
    if constexpr (I >= kSize) {
      return Error{seri::invalid_variant_index};
    } else {
      if (I == order) {
        return archive.Save(std::get<I>(val));
      }
      return Save<I + 1>(archive, order, val);
    }
  }

  seri::SeriResult Seri(seri::Archive auto& archive) const {
    auto order = this->index();
    TRY_RESULT((archive.Save(
        GetIndexByOrder(order, std::make_index_sequence<kSize>()))));

    return Save(archive, order, *this);
  }

  seri::SeriResult Deseri(seri::Archive auto& archive) {
    IndexType index{};
    TRY_RESULT((archive.Load(seri::Meta{index})));

    auto order = GetOrderByIndex(index, std::make_index_sequence<kSize>());

    return Load(archive, order, *this);
  }
};
}  // namespace ae

#endif  // AETHER_TYPES_VARIANT_TYPE_H_
