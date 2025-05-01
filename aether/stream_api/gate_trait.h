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

#ifndef AETHER_STREAM_API_GATE_TRAIT_H_
#define AETHER_STREAM_API_GATE_TRAIT_H_

#include <type_traits>

#include "aether/type_traits.h"
#include "aether/events/events.h"

namespace ae {

namespace _trait_internal {
template <typename T, typename Enable = void>
struct HasWriteIn : std::false_type {};
template <typename T>
struct HasWriteIn<T, std::void_t<decltype(&T::WriteIn)>> : std::true_type {};

template <typename T, typename Enable = void>
struct HasWriteOut : std::false_type {};
template <typename T>
struct HasWriteOut<T, std::void_t<decltype(&T::WriteOut)>> : std::true_type {};

template <typename T, typename Enable = void>
struct HasOverhead : std::false_type {};
template <typename T>
struct HasOverhead<T, std::void_t<decltype(&T::Overhead)>> : std::true_type {};

template <typename T, typename Enable = void>
struct HasOutDataEvent : std::false_type {};
template <typename T>
struct HasOutDataEvent<T, std::void_t<decltype(&T::out_data_event)>>
    : std::true_type {};

template <typename T, auto method>
struct WriteMethodTraits;

template <typename T, typename R, typename In, R (T::*method)(In data)>
struct WriteMethodTraits<T, method> {
  using RetType = std::decay_t<R>;
  using InType = std::decay_t<In>;
};

template <typename T, typename Enable = void>
struct WriteInTraits {
  using RetType = void;
  using InType = void;
};

template <typename T>
struct WriteInTraits<T, std::enable_if_t<HasWriteIn<T>::value>> {
  using RetType = typename WriteMethodTraits<T, &T::WriteIn>::RetType;
  using InType = typename WriteMethodTraits<T, &T::WriteIn>::InType;
};

template <typename T, typename Enable = void>
struct WriteOutTraits {
  using RetType = void;
  using InType = void;
};

template <typename T>
struct WriteOutTraits<T, std::enable_if_t<HasWriteOut<T>::value>> {
  using RetType = typename WriteMethodTraits<T, &T::WriteOut>::RetType;
  using InType = typename WriteMethodTraits<T, &T::WriteOut>::InType;
};

template <typename Es>
struct EventSubscriberTraits;

template <typename Arg>
struct EventSubscriberTraits<EventSubscriber<void(Arg const&)>> {
  using OutType = Arg;
};

template <typename T, typename Enable = void>
struct OutDataEventTraits {
  using OutType = void;
};

template <typename T>
struct OutDataEventTraits<T, std::enable_if_t<HasOutDataEvent<T>::value>> {
  using OutType = typename EventSubscriberTraits<
      decltype(std::declval<T>().out_data_event())>::OutType;
};

}  // namespace _trait_internal

template <typename T>
struct GateTrait {
  static constexpr bool kHasWriteIn = _trait_internal::HasWriteIn<T>::value;
  static constexpr bool kHasWriteOut = _trait_internal::HasWriteOut<T>::value;
  static constexpr bool kHasOverhead = _trait_internal::HasOverhead<T>::value;
  static constexpr bool kHasOutDataEvent =
      _trait_internal::HasOutDataEvent<T>::value;

  using WriteInInType = typename _trait_internal::WriteInTraits<T>::InType;
  using WriteInOutType = typename _trait_internal::WriteInTraits<T>::RetType;
  using WriteOutInType = typename _trait_internal::WriteOutTraits<T>::InType;
  using WriteOutOutType = typename _trait_internal::WriteOutTraits<T>::RetType;
  using OutDataEventType =
      typename _trait_internal::OutDataEventTraits<T>::OutType;
};
}  // namespace ae
#endif  // AETHER_STREAM_API_GATE_TRAIT_H_
