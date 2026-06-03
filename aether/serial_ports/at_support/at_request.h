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

#ifndef AETHER_SERIAL_PORTS_AT_SUPPORT_AT_REQUEST_H_
#define AETHER_SERIAL_PORTS_AT_SUPPORT_AT_REQUEST_H_

#include <tuple>
#include <array>
#include <string>
#include <utility>
#include <optional>
#include <functional>

#include "aether/executors/executors.h"
#include "aether/serial_ports/at_support/at_buffer.h"
#include "aether/serial_ports/at_support/at_support.h"
#include "aether/serial_ports/at_support/at_dispatcher.h"

namespace ae::at {

//// Making AT request

struct DefHandler {
  constexpr bool operator()(auto&&...) noexcept { return true; }
};

template <typename Handler = DefHandler>
  requires(std::is_invocable_r_v<bool, Handler, AtBuffer&, AtBuffer::iterator>)
struct Wait {
  std::string trigger;
  Handler handler;
};

template <>
struct Wait<DefHandler> {
  std::string trigger;
  DefHandler handler = {};
};

template <typename TCommand, typename... Ws>
struct RequestState {
  AtSupport& at_support;
  TCommand command;
  std::tuple<Ws...> waits;
};

template <typename Handler>
struct ListenerArgs {
  std::string trigger;
  Handler handler = {};
};

template <ex::receiver R, typename State>
class AtRequestOp {
 public:
  using StateType = State;
  static constexpr auto kWaitsCount =
      std::tuple_size_v<decltype(std::declval<StateType>().waits)>;

  explicit AtRequestOp(StateType&& state, R&& r) noexcept
      : receiver_{std::move(r)}, state_{std::move(state)} {}

  void start() noexcept {
    // setup waiters
    std::apply(
        [&](auto&&... ws) {
          MakeWaits(ListenerArgs{
              std::move(ws.trigger),
              [&, h_{std::move(ws.handler)}](auto&&... args) mutable noexcept {
                if (std::invoke(h_, std::forward<decltype(args)>(args)...)) {
                  // setup set value if all waits triggered
                  if (--waits_counter_ == 0) {
                    ex::set_value(std::move(receiver_));
                  }
                }
              },
          }...);
        },
        state_.waits);

    error_listener_.emplace(
        state_.at_support.dispatcher(), "ERROR",
        [&](auto&&...) noexcept { ex::set_error(std::move(receiver_), -1); });

    using CommandType = decltype(state_.command);
    auto res = std::invoke([&]() noexcept {
      if constexpr (std::is_same_v<std::string, CommandType>) {
        return state_.at_support.SendATCommand(state_.command);
      } else {
        return std::invoke(state_.command);
      }
    });
    if (!res) {
      ex::set_error(std::move(receiver_), res.error());
    } else if (waits_counter_ == 0) {
      ex::set_value(std::move(receiver_));
    }
  }

 private:
  template <std::size_t... Is, typename... La>
  void MakeWaitsImpl(std::index_sequence<Is...>, La&&... la) {
    // fill all listeners
    (listeners_[Is].emplace(state_.at_support.dispatcher(),
                            std::move(la.trigger), std::move(la.handler)),
     ...);
  }

  template <typename... La>
  void MakeWaits(La&&... la) {
    MakeWaitsImpl(std::make_index_sequence<sizeof...(La)>(),
                  std::forward<La>(la)...);
  }

  R receiver_;
  StateType state_;
  std::size_t waits_counter_ = kWaitsCount;
  std::array<std::optional<AtListener>, kWaitsCount> listeners_;
  std::optional<AtListener> error_listener_;
};

template <typename TCommand, typename... Ws>
class AtRequestSender {
 public:
  using StateType = RequestState<TCommand, Ws...>;

  using sender_concept = ex::sender_t;

  template <typename _, typename...>
    requires(std::is_same_v<_, AtRequestSender>)
  static consteval auto get_completion_signatures() noexcept
      -> ex::completion_signatures<ex::set_value_t(), ex::set_error_t(int)> {
    return {};
  }

  AtRequestSender(AtSupport& at_support, TCommand&& command, Ws&&... ws)
      : state{
            at_support,
            std::move(command),
            std::make_tuple(std::move(ws)...),
        } {}

  template <ex::receiver R>
  constexpr auto connect(R&& r) && noexcept {
    return AtRequestOp<R, StateType>{std::move(state), std::forward<R>(r)};
  }

 private:
  StateType state;
};

struct RequestMaker {
  template <typename... Ws>
  constexpr auto operator()(AtSupport& at_support, std::string c,
                            Ws&&... ws) const noexcept {
    return MakeAdapter(at_support, std::move(c), std::forward<Ws>(ws)...);
  }

  template <typename TCommand, typename... Ws>
    requires(std::is_invocable_r_v<Result<std::size_t, int>, TCommand>)
  auto operator()(AtSupport& at_support, TCommand&& c,
                  Ws&&... ws) const noexcept {
    return MakeAdapter(at_support, std::forward<TCommand>(c),
                       std::forward<Ws>(ws)...);
  }

  template <ex::sender S, typename... Ws>
  constexpr auto operator()(S&& s, AtSupport& at_support, std::string c,
                            Ws&&... ws) const noexcept {
    return MakeSender(std::forward<S>(s), at_support, std::move(c),
                      std::forward<Ws>(ws)...);
  }

  template <ex::sender S, typename TCommand, typename... Ws>
    requires(std::is_invocable_r_v<Result<std::size_t, int>, TCommand>)
  constexpr auto operator()(S&& s, AtSupport& at_support, TCommand&& c,
                            Ws&&... ws) const noexcept {
    return MakeSender(std::forward<S>(s), at_support, std::forward<TCommand>(c),
                      std::forward<Ws>(ws)...);
  }

  template <typename TCommand, typename... Ws>
  constexpr auto MakeAdapter(AtSupport& at_support, TCommand&& c,
                             Ws&&... ws) const noexcept {
    return ex::__closure<RequestMaker, AtSupport&, TCommand, Ws...>{
        *this, at_support, std::forward<TCommand>(c), std::forward<Ws>(ws)...};
  }

  template <ex::sender S, typename TCommand, typename... Ws>
  constexpr auto MakeSender(S&& s, AtSupport& at_support, TCommand&& c,
                            Ws&&... ws) const noexcept {
    return ex::let_value(
        std::forward<S>(s),
        [&at_support, c_{std::forward<TCommand>(c)},
         ws_{std::tuple{std::forward<Ws>(ws)...}}]() mutable noexcept {
          return std::apply(
              [&](auto&&... args) noexcept -> decltype(auto) {
                return AtRequestSender{at_support, std::move(c_),
                                       std::move(args)...};
              },
              std::move(ws_));
        });
  }
};

static constexpr inline auto MakeRequest = RequestMaker{};
}  // namespace ae::at

#endif  // AETHER_SERIAL_PORTS_AT_SUPPORT_AT_REQUEST_H_
