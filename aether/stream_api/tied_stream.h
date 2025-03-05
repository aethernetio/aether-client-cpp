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

#ifndef AETHER_STREAM_API_TIED_STREAM_H_
#define AETHER_STREAM_API_TIED_STREAM_H_

#include <tuple>
#include <utility>
#include <type_traits>

#include "aether/type_traits.h"
#include "aether/stream_api/istream.h"

namespace ae {

template <typename... TStreams>
struct StreamTrait {
  static constexpr auto LeftNum = 0;
  static constexpr auto RightNum = sizeof...(TStreams) - 1;

  template <std::size_t I, typename _ = void>
  struct NType {
    using type = std::tuple_element_t<I, std::tuple<TStreams...>>;
  };

  template <std::size_t I>
  struct NType<I, std::enable_if_t<IsPointerLike<std::tuple_element_t<
                      I, std::tuple<TStreams...>>>::value>> {
    using type = std::decay_t<decltype(*std::declval<std::tuple_element_t<
                                           I, std::tuple<TStreams...>>>())>;
  };

  template <std::size_t I, typename _ = void>
  struct NGate {
    using in = typename NType<I>::type;
    using out = typename NType<I>::type;
  };

  template <std::size_t I>
  struct NGate<I, std::enable_if_t<
                      _traits::IsStreamType<typename NType<I>::type>::value>> {
    using in = typename NType<I>::type::InGate;
    using out = typename NType<I>::type::OutGate;
  };

  using LeftGate = typename NGate<LeftNum>::in;
  using RightGate = typename NGate<RightNum>::out;

  using StreamType =
      Stream<typename LeftGate::TypeIn, typename LeftGate::TypeOut,
             typename RightGate::TypeIn, typename RightGate::TypeOut>;
};

template <typename... TStreams>
class TiedStream : public StreamTrait<std::decay_t<TStreams>...>::StreamType {
 public:
  using Trait = StreamTrait<std::decay_t<TStreams>...>;

  explicit TiedStream(TStreams... streams)
      : streams_{std::forward<TStreams>(streams)...} {
    TieStreams(std::make_index_sequence<sizeof...(TStreams)>());
  }

  typename Trait::StreamType::InGate& in() override {
    auto& value = GetValue<Trait::LeftNum>();
    using val_type = decltype(value);
    if constexpr (_traits::IsStreamType<val_type>::value) {
      return value.in();
    } else {
      return value;
    }
  }

  void LinkOut(typename Trait::StreamType::OutGate& out) override {
    if constexpr (_traits::IsLinkable<typename Trait::RightGate>::value) {
      GetValue<Trait::RightNum>().LinkOut(out);
    } else {
      assert(false);
    }
  }

  template <std::size_t I>
  auto& GetElement() {
    return std::get<I>(streams_);
  }

 private:
  template <std::size_t I>
  auto& GetValue() {
    using type = std::tuple_element_t<I, decltype(streams_)>;
    if constexpr (IsPointerLike<type>::value) {
      return *std::get<I>(streams_);
    } else {
      return std::get<I>(streams_);
    }
  }

  template <std::size_t... Is>
  void TieStreams(std::index_sequence<Is...>) {
    Tie(GetValue<Is>()...);
  }

  std::tuple<TStreams...> streams_;
};

template <typename... U>
TiedStream(U&&...) -> TiedStream<U...>;
}  // namespace ae

namespace std {
template <std::size_t I, typename... T>
auto& get(ae::TiedStream<T...>& stream) {
  return stream.template GetElement<I>();
}
}  // namespace std

#endif  // AETHER_STREAM_API_TIED_STREAM_H_
