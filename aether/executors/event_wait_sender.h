/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_EXECUTORS_EVENT_WAIT_SENDER_H_
#define AETHER_EXECUTORS_EVENT_WAIT_SENDER_H_

#include <type_traits>
#include <utility>

#include "aether-miscpp/meta/ignore_t.h"
#include "aether-miscpp/meta/type_list.h"
#include "aether-miscpp/types/result.h"

#include "aether/warning_disable.h"

// IWYU pragma: begin_exports
DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include <stdexec/execution.hpp>
DISABLE_WARNING_POP()

#include "aether/events/events.h"

namespace ae::ex {
namespace event_wait_sender_internal {
template <typename T>
struct IsEventType : std::false_type {};

template <typename Es, typename Signature>
struct IsEventType<events::EventObject<Es, Signature>> : std::true_type {};

template <typename T>
concept EventType = IsEventType<T>::value;

template <typename T>
struct EventObjectTrait {};

template <typename Es, typename... Args>
struct EventObjectTrait<events::EventObject<Es, void(Args...)>> {
  using event_system = Es;
  using arg_list = TypeList<Args...>;
};

struct EventHandler {
  decltype(auto) operator()(auto&&... args) const noexcept {
    static_assert(sizeof...(args) <= 1,
                  "Default handler could handle only one or zero arguments");
    return HandleArg(std::forward<decltype(args)>(args)...);
  }

  template <typename T>
  static decltype(auto) HandleArg(T&& first) noexcept {
    return std::forward<T>(first);
  }

  static void HandleArg() noexcept {
    ;  // nothing
  }
};

template <typename R, EventType E, typename Handler, typename Res>
class OpState {
 public:
  using event_system = typename EventObjectTrait<E>::event_system;
  using Subscription = events::SubscriptionObject<event_system>;
  using result_type = Res;

  constexpr OpState(R&& receiver, E const& event, Handler&& handler)
      : receiver_{std::move(receiver)},
        event_{&event},
        handler_{std::move(handler)} {}

  constexpr void start() noexcept {
    sub_ = event_->Subscribe([this](auto&&... args) noexcept {
      if constexpr (std::is_void_v<result_type>) {
        handler_(std::forward<decltype(args)>(args)...);
        stdexec::set_value(std::move(receiver_));
      } else {
        auto&& res = handler_(std::forward<decltype(args)>(args)...);
        if constexpr (IsResultType_v<result_type>) {
          if (res) {
            if constexpr (IsIgnore_v<typename result_type::value_type>) {
              stdexec::set_value(std::move(receiver_));
            } else {
              stdexec::set_value(std::move(receiver_), std::move(res).value());
            }
          } else {
            stdexec::set_error(std::move(receiver_), std::move(res).error());
          }
        } else {
          stdexec::set_value(std::move(receiver_), std::move(res));
        }
      }
      sub_.Reset();
    });
  }

 private:
  R receiver_;
  E const* event_;
  Handler handler_;
  Subscription sub_;
};

template <EventType E, typename Handler = EventHandler>
class Sender {
  template <typename... Args>
  static consteval auto GetInvokeResult(TypeList<Args...>)
      -> std::invoke_result_t<Handler, Args...>;

 public:
  using invoke_result = std::decay_t<decltype(GetInvokeResult(
      typename EventObjectTrait<E>::arg_list{}))>;
  using sender_concept = stdexec::sender_t;

  /**
   * Extract completion signatures from handler's invoke_result
   * If invoke_result is void, return just set_value_t()
   * If invoke_result some type return set_value_t(invoke_result)
   * If invoke_result is Result type:
   * if value_type is Ignore type return set_value_t(), set_error_t(error_type)
   * if value_type is some type return set_value_t(value_type),
   * set_error_t(error_type)
   */
  template <typename _, typename...>
  static consteval auto get_completion_signatures() {
    if constexpr (IsResultType_v<invoke_result>) {
      using value_type = typename invoke_result::value_type;
      using error_type = typename invoke_result::error_type;
      if constexpr (IsIgnore_v<value_type>) {
        return stdexec::completion_signatures<
            stdexec::set_value_t(), stdexec::set_error_t(error_type)>{};
      } else {
        return stdexec::completion_signatures<stdexec::set_value_t(value_type),
                                              stdexec::set_error_t(
                                                  error_type)>{};
      }
    } else if constexpr (!std::is_void_v<invoke_result>) {
      return stdexec::completion_signatures<stdexec::set_value_t(
          invoke_result)>{};
    } else {
      return stdexec::completion_signatures<stdexec::set_value_t()>{};
    }
  }

  constexpr Sender(E const& event, Handler&& handler) noexcept
      : event_{&event}, handler_{std::move(handler)} {}

  template <stdexec::receiver R>
  constexpr auto connect(R&& receiver) && noexcept {
    return OpState<std::decay_t<R>, E, Handler, invoke_result>{
        std::forward<R>(receiver), *event_, std::move(handler_)};
  }

 private:
  E const* event_;
  Handler handler_;
};

/**
 * \brief Creates a sender which after connect and start subscribes to event
 * and waits for it's result
 */
struct EventWait {
  template <EventType E, typename Handler = EventHandler>
  constexpr auto operator()(E const& e, Handler&& handler = {}) const noexcept {
    return Sender<E, std::decay_t<Handler>>{e, std::forward<Handler>(handler)};
  }
};
}  // namespace event_wait_sender_internal

static constexpr inline auto event_wait =
    event_wait_sender_internal::EventWait{};

}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_EVENT_WAIT_SENDER_H_
