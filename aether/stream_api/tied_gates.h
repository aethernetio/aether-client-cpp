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

#ifndef AETHER_STREAM_API_TIED_GATES_H_
#define AETHER_STREAM_API_TIED_GATES_H_

#include "aether/events/event_subscription.h"

#include "aether/type_traits.h"
#include "aether/stream_api/gate_trait.h"

namespace ae {
namespace gates_internal {
// Overload for one gate
template <typename T, typename LeftGate>
auto TiedWriteInImpl(T&& data, LeftGate&& left_gate) {
  if constexpr (GateTrait<std::decay_t<LeftGate>>::kHasWriteIn) {
    return std::forward<LeftGate>(left_gate).WriteIn(std::forward<T>(data));
  } else {
    return std::forward<T>(data);
  }
}

// Gates should be in normal order from left to right
template <typename T, typename LeftGate, typename... TGates>
auto TiedWriteInImpl(T&& data, LeftGate&& left_gate, TGates&&... gates) {
  if constexpr (GateTrait<std::decay_t<LeftGate>>::kHasWriteIn) {
    if constexpr (std::is_void_v<typename GateTrait<
                      std::decay_t<LeftGate>>::WriteInOutType>) {
      std::forward<LeftGate>(left_gate).WriteIn(std::forward<T>(data));
      return;
    } else {
      return TiedWriteInImpl(
          std::forward<LeftGate>(left_gate).WriteIn(std::forward<T>(data)),
          std::forward<TGates>(gates)...);
    }
  } else {
    return TiedWriteInImpl(std::forward<T>(data),
                           std::forward<TGates>(gates)...);
  }
}

template <typename T>
decltype(auto) TiedWriteOutImpl(T&& data) {
  return std::forward<T>(data);
}

// Overload for one gate
template <typename T, typename RightGate>
auto TiedWriteOutImpl(T&& data, RightGate&& right_gate) {
  if constexpr (GateTrait<std::decay_t<RightGate>>::kHasWriteOut) {
    return std::forward<RightGate>(right_gate).WriteOut(std::forward<T>(data));
  } else {
    return std::forward<T>(data);
  }
}

// gates must be in reverse order from right to left
template <typename T, typename RightGate, typename... TGates>
auto TiedWriteOutImpl(T&& data, RightGate&& right_gate, TGates&&... gates) {
  if constexpr (GateTrait<std::decay_t<RightGate>>::kHasWriteOut) {
    if constexpr (std::is_void_v<typename GateTrait<
                      std::decay_t<RightGate>>::WriteOutOutType>) {
      std::forward<RightGate>(right_gate).WriteOut(std::forward<T>(data));
      return;
    } else {
      return TiedWriteOutImpl(
          std::forward<RightGate>(right_gate).WriteOut(std::forward<T>(data)),
          std::forward<TGates>(gates)...);
    }
  } else {
    return TiedWriteOutImpl(std::forward<T>(data),
                            std::forward<TGates>(gates)...);
  }
}

template <typename LeftGate, typename... TGates>
void TiedOverheadImpl(std::size_t& overhead, LeftGate&& left_gate,
                      TGates&&... gates) {
  if constexpr (GateTrait<std::decay_t<LeftGate>>::kHasOverhead) {
    overhead += std::forward<LeftGate>(left_gate).Overhead();
  }
  if constexpr (sizeof...(TGates) != 0) {
    return TiedOverheadImpl(overhead, std::forward<TGates>(gates)...);
  }
}

template <typename RightGate, typename... TGates>
struct Subscriber {
  template <typename THandler>
  Subscription Make([[maybe_unused]] THandler&& handler, RightGate right_gate,
                    TGates... gates) {
    using DataType =
        typename GateTrait<std::decay_t<RightGate>>::OutDataEventType;
    using res_type = decltype(TiedWriteOutImpl(std::declval<DataType const&>(),
                                               std::declval<TGates&>()...));
    if constexpr (std::is_void_v<res_type>) {
      return std::forward<RightGate>(right_gate)
          .out_data_event()
          .Subscribe([&](auto&& data) {
            TiedWriteOutImpl(std::forward<decltype(data)>(data),
                             std::forward<TGates>(gates)...);
          });
    } else {
      return std::forward<RightGate>(right_gate)
          .out_data_event()
          .Subscribe(
              [&, handler{std::forward<THandler>(handler)}](auto&& data) {
                handler(TiedWriteOutImpl(std::forward<decltype(data)>(data),
                                         std::forward<TGates>(gates)...));
              });
    }
  }

  template <typename THandler, typename U, typename... UGates>
  Subscription Make(THandler&& handler, U&&, UGates&&... gates) {
    return Make(std::forward<THandler>(handler),
                std::forward<UGates>(gates)...);
  }
};

template <typename RightGate, typename... TGates>
constexpr auto MakeSubscribers() {
  if constexpr (GateTrait<std::decay_t<RightGate>>::kHasOutDataEvent) {
    if constexpr (sizeof...(TGates) == 0) {
      return std::tuple<Subscriber<RightGate>>{};
    } else {
      return std::tuple_cat(std::tuple<Subscriber<RightGate, TGates...>>{},
                            MakeSubscribers<TGates...>());
    }
  } else {
    if constexpr (sizeof...(TGates) == 0) {
      return std::tuple<>{};
    } else {
      return MakeSubscribers<TGates...>();
    }
  }
}

template <typename Subscribers, std::size_t index, typename THandler,
          typename... TGates>
constexpr auto SubscribeGateOutDataEvent(THandler&& handler,
                                         TGates&&... gates) {
  using subscriber_t = std::tuple_element_t<index, Subscribers>;
  return subscriber_t{}.Make(std::forward<THandler>(handler),
                             std::forward<TGates>(gates)...);
}

template <typename Subscribers, std::size_t... indexes, typename THandler,
          typename... TGates>
constexpr auto MakeSubscribeGateOutDataEvent(
    std::index_sequence<indexes...> const&, THandler&& handler,
    TGates&&... gates) {
  return std::array{SubscribeGateOutDataEvent<Subscribers, indexes>(
      std::forward<THandler>(handler), std::forward<TGates>(gates)...)...};
}

template <typename THandler, typename... TGates>
constexpr auto TiedEventOutDataImpl([[maybe_unused]] THandler&& handler,
                                    [[maybe_unused]] TGates&&... gates) {
  using subscribers_list = decltype(MakeSubscribers<TGates...>());
  constexpr auto subscribers_count = std::tuple_size_v<subscribers_list>;

  if constexpr (subscribers_count == 0) {
    return std::array<Subscription, 0>{};
  } else {
    return MakeSubscribeGateOutDataEvent<subscribers_list>(
        std::make_index_sequence<subscribers_count>(),
        std::forward<THandler>(handler), std::forward<TGates>(gates)...);
  }
}
}  // namespace gates_internal

/**
 * \brief Performs top to bottom chain through all gates
 * If Gate is WriteIn gate WriteIn is called.
 */
template <typename T, typename... TGates>
auto TiedWriteIn(T&& data, TGates&&... gates) {
  return gates_internal::TiedWriteInImpl(std::forward<T>(data),
                                         std::forward<TGates>(gates)...);
}

/**
 * \brief Performs bottom to top chain through all gates
 * If Gate is WriteOut gate WriteOut is called, if WriteOut returns void, chain
 * is interrupted.
 */
template <typename T, typename... TGates>
auto TiedWriteOut(T&& data, TGates&&... gates) {
  return ApplyRerverse(
      [&](auto&&... gs) {
        return gates_internal::TiedWriteOutImpl(
            std::forward<T>(data), std::forward<decltype(gs)>(gs)...);
      },
      std::forward<TGates>(gates)...);
}

/**
 * \brief Get the summ of overhead from all of gates.
 */
template <typename... TGates>
std::size_t TiedOverhead(TGates&&... gates) {
  std::size_t res = 0;
  gates_internal::TiedOverheadImpl(res, std::forward<TGates>(gates)...);
  return res;
}

template <typename THandler, typename... TGates>
constexpr auto TiedEventOutData(THandler&& handler, TGates&&... gates) {
  return ApplyRerverse(
      [&](auto&&... gates) {
        return gates_internal::TiedEventOutDataImpl(
            std::forward<THandler>(handler),
            std::forward<decltype(gates)>(gates)...);
      },
      std::forward<TGates>(gates)...);
}

}  // namespace ae

#endif  // AETHER_STREAM_API_TIED_GATES_H_
