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

#include <tuple>
#include <bitset>
#include <cstdint>
#include <type_traits>
#include <functional>

#include "aether/type_traits.h"
#include "aether/reflect/reflect.h"

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
  using ValueType =
      decltype(SelectValueType<kBitsCount, std::uint8_t, std::uint16_t,
                               std::uint32_t, std::uint64_t>());

  explicit NullableValues(TArgs&... args) : arg_refs_{args...} {}

  template <typename Stream>
  void Load(Stream& is) {
    ValueType mask_value{};
    is >> mask_value;
    LoadValue(is, std::bitset<kBitsCount>{mask_value},
              std::make_index_sequence<kBitsCount>());
  }

  template <typename Stream>
  void Save(Stream& os) const {
    auto value = BuildMask();
    os << value;
    SaveValues(os, std::make_index_sequence<kBitsCount>());
  }

 private:
  ValueType BuildMask() const {
    return BuildMaskImpl(std::make_index_sequence<kBitsCount>());
  }

  template <std::size_t... Is>
  ValueType BuildMaskImpl(std::index_sequence<Is...>) const {
    std::bitset<kBitsCount> mask;
    (std::invoke([&]() {
       auto& v = std::get<Is>(arg_refs_);
       if constexpr (IsOptional<std::decay_t<decltype(v)>>::value) {
         mask.set(Is, !v.has_value());
       }
     }),
     ...);
    return static_cast<ValueType>(mask.to_ulong());
  }

  template <typename Stream, std::size_t... Is>
  void SaveValues(Stream& os, std::index_sequence<Is...>) const {
    (std::invoke([&]() {
       auto& v = std::get<Is>(arg_refs_);
       if constexpr (IsOptional<std::decay_t<decltype(v)>>::value) {
         if (v.has_value()) {
           os << v.value();
         }
       } else {
         os << v;
       }
     }),
     ...);
  }

  template <typename Stream, std::size_t... Is>
  void LoadValue(Stream& is, std::bitset<kBitsCount> mask,
                 std::index_sequence<Is...>) {
    (std::invoke([&]() {
       auto& v = std::get<Is>(arg_refs_);
       using VType = std::decay_t<decltype(v)>;
       if constexpr (IsOptional<VType>::value) {
         // if not it's not nullable
         if (!mask[Is]) {
           typename VType::value_type value;
           is >> value;
           v = value;
         }
       } else {
         is >> v;
       }
     }),
     ...);
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
  template <typename Stream>
  void Load(Stream& is) {
    auto values = BuildNullableValues(*this);
    values.Load(is);
  }

  template <typename Stream>
  void Save(Stream& os) const {
    auto values = BuildNullableValues(const_cast<NullableType&>(*this));
    values.Save(os);
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
    auto refl = reflect::Reflection{obj};
    return refl.Apply(
        [](auto&... fields) { return BuildArgListImpl<U>(fields...); });
  }

  template <typename TSelf>
  static auto BuildNullableValues(TSelf& self) {
    static_assert(reflect::IsReflectable<T>::value,
                  "T must be reflecatable type");
    auto args_list = BuildArgList(static_cast<T&>(self));
    return std::apply([](auto&... args) { return NullableValues{args...}; },
                      args_list);
  }
};

}  // namespace ae

#endif  // AETHER_TYPES_NULLABLE_TYPE_H_
