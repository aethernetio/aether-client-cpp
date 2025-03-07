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

#ifndef AETHER_TYPE_TRAITS_H_
#define AETHER_TYPE_TRAITS_H_

#include <array>
#include <tuple>
#include <string>
#include <optional>
#include <type_traits>

namespace ae {

namespace _internal {
template <typename T, T... N, std::size_t... Indices>
constexpr auto reverse_sequence_helper(std::integer_sequence<T, N...>,
                                       std::index_sequence<Indices...>) {
  constexpr auto array = std::array<T, sizeof...(N)>{N...};
  return std::integer_sequence<T, array[sizeof...(Indices) - Indices - 1]...>();
}

template <typename T, T min, T... N>
constexpr auto make_range_sequence_helper(std::integer_sequence<T, N...>) {
  return std::integer_sequence<T, (N + min)...>();
}
}  // namespace _internal

template <typename T, T... N>
constexpr auto reverse_sequence(std::integer_sequence<T, N...> sequence) {
  return _internal::reverse_sequence_helper(
      sequence, std::make_index_sequence<sizeof...(N)>());
}

template <typename T, T from, T to>
constexpr auto make_range_sequence() {
  if constexpr (from <= to) {
    return _internal::make_range_sequence_helper<T, from>(
        std::make_integer_sequence<T, to - from + 1>());
  } else {
    // make reverse sequence from bigger to lesser
    return reverse_sequence(_internal::make_range_sequence_helper<T, to>(
        std::make_integer_sequence<T, from - to + 1>()));
  }
}

template <typename T>
struct IsString : std::false_type {};

template <>
struct IsString<std::string> : std::true_type {};

template <typename T>
struct IsStringView : std::false_type {};

template <>
struct IsStringView<std::string_view> : std::true_type {};

template <typename T, typename _ = void>
struct IsContainer : std::false_type {};

template <typename T>
struct IsContainer<
    T, std::conditional_t<
           false,
           std::void_t<typename T::value_type, typename T::size_type,
                       typename T::iterator, typename T::const_iterator,
                       decltype(std::declval<T>().size()),
                       decltype(std::declval<T>().begin()),
                       decltype(std::declval<T>().end()),
                       decltype(std::declval<T>().cbegin()),
                       decltype(std::declval<T>().cend())>,
           void>> : public std::true_type {};

template <typename T, typename _ = void>
struct IsAssociatedContainer : std::false_type {};

template <typename T>
struct IsAssociatedContainer<
    T, std::conditional_t<
           false,
           std::void_t<typename T::key_type, typename T::mapped_type,
                       typename T::value_type, typename T::size_type,
                       typename T::iterator, typename T::const_iterator,
                       decltype(std::declval<T>().size()),
                       decltype(std::declval<T>().begin()),
                       decltype(std::declval<T>().end()),
                       decltype(std::declval<T>().cbegin()),
                       decltype(std::declval<T>().cend())>,
           void>> : public std::true_type {};

template <typename T1, typename T2>
struct IsAbleToCast {
  static constexpr bool value =
      std::is_base_of_v<T1, T2> || std::is_base_of_v<T2, T1>;
};

template <typename T>
struct IsOptional : std::false_type {};

template <typename T>
struct IsOptional<std::optional<T>> : std::true_type {};

/**
 * \brief Type has std::optional like interface, looks like optional.
 */
template <typename T, typename _ = void>
struct IsOptionalLike : std::false_type {};

template <typename T>
struct IsOptionalLike<
    T, std::void_t<decltype(!!std::declval<T>()), decltype(*std::declval<T>())>>
    : std::true_type {};

/**
 * \brief Type has a smart pointer like interface, looks like pointer.
 */
template <typename T, typename Enable = void>
struct IsPointerLike : std::false_type {};

template <typename T>
struct IsPointerLike<T,
                     std::void_t<decltype(static_cast<bool>(std::declval<T>())),
                                 decltype(*std::declval<T>()),
                                 decltype(std::declval<T>().operator->()),
                                 decltype(std::declval<T>().get())>>
    : std::true_type {};

/**
 * \brief Type is functor object with given signature
 */
template <typename T, typename TSignature, typename _ = void>
struct IsFunctor : std::false_type {};

template <typename T, typename TRes, typename... TArgs>
struct IsFunctor<T, TRes(TArgs...),
                 std::enable_if_t<std::is_invocable_r_v<TRes, T, TArgs...>>>
    : std::true_type {};

/**
 * \brief Type is function pointer
 */
template <typename TFuncPtr, typename T, typename _ = void>
struct IsFunctionPtr : std::false_type {};

template <typename TFuncPtr, typename T>
struct IsFunctionPtr<
    TFuncPtr, T,
    std::void_t<decltype(static_cast<TFuncPtr>(std::declval<T>()))>>
    : std::true_type {};

template <typename TSignature>
struct FunctionSignatureImpl;

template <typename TRet, typename... TArgs>
struct FunctionSignatureImpl<TRet(TArgs...)> {
  using Signature = TRet(TArgs...);
  using FuncPtr = TRet (*)(TArgs...);
};

template <typename TRes, typename... TArgs>
auto GetSignatureImpl(TRes (*)(TArgs...))
    -> FunctionSignatureImpl<TRes(TArgs...)>;

template <typename TClass, typename TRes, typename... TArgs>
auto GetSignatureImpl(TRes (TClass::*)(TArgs...) const)
    -> FunctionSignatureImpl<TRes(TArgs...)>;

template <typename TClass, typename TRes, typename... TArgs>
auto GetSignatureImpl(TRes (TClass::*)(TArgs...))
    -> FunctionSignatureImpl<TRes(TArgs...)>;

/**
 * \brief Get a signature of functor object, or function pointer.
 */
template <typename TFunc, typename FuncSignatureImp =
                              decltype(GetSignatureImpl(std::declval<TFunc>()))>
struct FunctionSignature {
  using Signature = typename FuncSignatureImp::Signature;
  using FuncPtr = typename FuncSignatureImp::FuncPtr;
};

/**
 * \brief Get I'th argument in template args list.
 */
template <std::size_t I, auto... args>
constexpr auto ArgAt() {
  return std::get<I>(std::forward_as_tuple(args...));
}

/**
 * \brief Get I'th argument in args list.
 */
template <std::size_t I, typename... TArgs>
constexpr auto&& ArgAt(TArgs&&... args) {
  return std::forward<std::tuple_element_t<I, std::tuple<TArgs...>>>(
      std::get<I>(std::forward_as_tuple(args...)));
}

/**
 * \brief Get I'th type in template type list.
 */
template <std::size_t I, typename... Ts>
constexpr auto TypeAt() -> std::tuple_element_t<I, std::tuple<Ts...>>;

template <std::size_t I, typename... Ts>
using TypeAt_t = decltype(TypeAt<I, Ts...>());

/**
 * \brief Pass tuple type list as template parameters to template T
 */
template <template <typename...> typename T, typename Tuple>
struct TupleToTemplate;

template <template <typename...> typename T, typename... Ts>
struct TupleToTemplate<T, std::tuple<Ts...>> {
  using type = T<Ts...>;
};

/**
 * \brief static cast with save constness
 */
template <typename T, typename U>
constexpr T StaticConst(U& u) {
  return static_cast<T>(u);
}

template <typename T, typename U>
constexpr auto& StaticConst(U const& u) {
  return static_cast<std::add_const_t<std::remove_reference_t<T>>&>(u);
}

}  // namespace ae
#endif  // AETHER_TYPE_TRAITS_H_ */
