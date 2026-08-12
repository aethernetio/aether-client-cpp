/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_TYPES_NULLABLE_TYPE_H_
#define AETHER_TYPES_NULLABLE_TYPE_H_

#include <bitset>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-miscpp/serialization/serialization.h"
#include "aether/type_traits.h"

namespace ae {
template <typename... TArgs>
class NullableValues {
  template <std::size_t Count, typename T, typename... Types>
  static constexpr auto SelectValueType() {
    if constexpr (Count > std::numeric_limits<T>::digits) {
      SelectValueType<Count, Types...>();
    } else {
      return T{};
    }
  }

 public:
  static constexpr std::size_t kBitsCount = sizeof...(TArgs);
  using MaskType =
      decltype(SelectValueType<kBitsCount, std::uint8_t, std::uint16_t,
                               std::uint32_t, std::uint64_t>());

  explicit NullableValues(TArgs&... args) : arg_refs_{args...} {}

  seri::SeriResult Seri(seri::Archive auto& archive) const {
    TRY_RESULT(archive.Save(BuildMask()));
    return SeriValues(archive, std::make_index_sequence<kBitsCount>{});
  }

  seri::SeriResult Deseri(seri::Archive auto& archive) {
    MaskType mask_value{};
    TRY_RESULT(archive.Load(seri::Meta{mask_value}));
    return DeseriValues(archive, std::bitset<kBitsCount>{mask_value},
                        std::make_index_sequence<kBitsCount>{});
  }

 private:
  MaskType BuildMask() const {
    return BuildMaskImpl(std::make_index_sequence<kBitsCount>());
  }

  template <std::size_t... Is>
  constexpr MaskType BuildMaskImpl(std::index_sequence<Is...>) const {
    std::bitset<kBitsCount> mask;

    (std::invoke([&]() {
       auto& v = std::get<Is>(arg_refs_);
       if constexpr (IsOptional<std::decay_t<decltype(v)>>::value) {
         mask.set(Is, !v.has_value());
       }
     }),
     ...);
    return static_cast<MaskType>(mask.to_ulong());
  }

  template <std::size_t I>
  seri::SeriResult SeriValue(seri::Archive auto& archive) const {
    auto const& value = std::get<I>(arg_refs_);
    if constexpr (IsOptional<std::decay_t<decltype(value)>>::value) {
      if (value.has_value()) {
        return archive.Save(value.value());
      }
      return Ok{seri::good};
    } else {
      return archive.Save(value);
    }
  }

  template <std::size_t... Is>
  seri::SeriResult SeriValues(seri::Archive auto& archive,
                              std::index_sequence<Is...>) const {
    seri::SeriResult result{Ok{seri::good}};
    ((result.IsOk() ? result = SeriValue<Is>(archive) : result), ...);
    return result;
  }

  template <std::size_t I>
  seri::SeriResult DeseriValue(seri::Archive auto& archive,
                               std::bitset<kBitsCount> const& mask) {
    auto& value = std::get<I>(arg_refs_);
    using ValueType = std::decay_t<decltype(value)>;
    if constexpr (IsOptional<ValueType>::value) {
      if (!mask[I]) {
        typename ValueType::value_type loaded_value{};
        TRY_RESULT(archive.Load(loaded_value));
        value = std::move(loaded_value);
      } else {
        value = std::nullopt;
      }
      return Ok{seri::good};
    } else {
      return archive.Load(value);
    }
  }

  template <std::size_t... Is>
  seri::SeriResult DeseriValues(seri::Archive auto& archive,
                                std::bitset<kBitsCount> const& mask,
                                std::index_sequence<Is...>) {
    seri::SeriResult result{Ok{seri::good}};
    ((result.IsOk() ? result = DeseriValue<Is>(archive, mask) : result), ...);
    return result;
  }

  std::tuple<TArgs&...> arg_refs_;
};

/**
 * \brief This add serialization as Nullably type for T.
 * Inherit T from NullableType<T>.
 * T must be a Reflecatable type.
 * It builds a bit mask for all fields of T and its parents.
 * If field is optional type it may exist or not. If it's not not the bit is set
 * to 1.
 * The not null fields are not loaded from the stream.
 */
template <typename T>
class NullableType {
 public:
  seri::SeriResult Seri(seri::Archive auto& archive) const {
    auto values = BuildNullableValues(*this);
    return values.Seri(archive);
  }

  seri::SeriResult Deseri(seri::Archive auto& archive) {
    auto values = BuildNullableValues(*this);
    return values.Deseri(archive);
  }

 private:
  template <typename U, typename Arg, typename... Args>
  static auto BuildArgListImpl(Arg& arg, Args&... args) {
    if constexpr (std::is_base_of_v<Arg, U>) {
      return std::tuple_cat(BuildArgList<Arg>(arg),
                            BuildArgListImpl<U>(args...));
    } else {
      return std::tuple_cat(std::tuple<Arg&>{arg},
                            BuildArgListImpl<U>(args...));
    }
  }

  template <typename U>
  constexpr static auto BuildArgListImpl() {
    return std::tuple{};
  }

  template <typename U>
  static auto BuildArgList(U& obj) {
    auto refl = reflect::make_reflection(obj);
    return refl.Apply(
        [](auto&... fields) { return BuildArgListImpl<U>(fields...); });
  }

  template <typename TSelf>
  static auto BuildNullableValues(TSelf& self) {
    static_assert(reflect::Reflectable<T>, "T must be reflecatable type");
    using ReflectedType =
        std::conditional_t<std::is_const_v<TSelf>, T const, T>;
    auto args_list = BuildArgList(static_cast<ReflectedType&>(self));
    return std::apply([](auto&... args) { return NullableValues{args...}; },
                      args_list);
  }
};

}  // namespace ae

#endif  // AETHER_TYPES_NULLABLE_TYPE_H_
