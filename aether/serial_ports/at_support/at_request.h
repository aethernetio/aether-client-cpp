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

#include <list>
#include <string>
#include <variant>
#include <vector>
#include <cstdint>

#include "aether/common.h"
#include "aether/events/events.h"
#include "aether/actions/action.h"
#include "aether/actions/action_ptr.h"
#include "aether/types/state_machine.h"
#include "aether/types/small_function.h"
#include "aether/events/multi_subscription.h"
#include "aether/serial_ports/at_support/at_buffer.h"
#include "aether/serial_ports/at_support/at_dispatcher.h"
#include "aether/serial_ports/at_support/at_write_action.h"

namespace ae {
class AtSupport;
class AtRequest final : public Action<AtRequest> {
  struct Internal {};

 public:
  enum class State : std::uint8_t {
    kMakeRequest,
    kWaitResponse,
    kSuccess,
    kError,
  };

  using CommandMaker = SmallFunction<ActionPtr<AtWriteAction>()>;

  struct Command {
    std::variant<std::string, CommandMaker> command;
  };

  using ResHandler =
      std::function<bool(AtBuffer& buffer, AtBuffer::iterator pos)>;

  struct Wait {
    std::string expected;
    Duration timeout;
    ResHandler handler = [](auto&, auto) { return true; };
  };

  class WaitObserver final : public IAtObserver {
   public:
    WaitObserver(AtDispatcher& dispatcher, Wait wait);
    ~WaitObserver();

    AE_CLASS_NO_COPY_MOVE(WaitObserver);

    void Observe(AtBuffer& buffer, AtBuffer::iterator pos) override;

    Event<void(bool)>::Subscriber observed_event();
    Duration timeout() const;
    bool observed() const;

   private:
    AtDispatcher* dispatcher_;
    Wait wait_;
    Event<void(bool)> observed_event_;
    bool observed_;
  };

  template <typename TCommand, typename... Waits>
  AtRequest(ActionContext action_context, AtDispatcher& dispatcher,
            AtSupport& at_support, TCommand&& at_command, Waits&&... waits)
      : AtRequest{action_context, dispatcher, at_support,
                  Command{std::forward<TCommand>(at_command)},
                  std::vector<Wait>{std::forward<Waits>(waits)...}} {}

  UpdateStatus Update(TimePoint current_time);

 private:
  AtRequest(ActionContext action_context, AtDispatcher& dispatcher,
            AtSupport& at_support, Command at_command, std::vector<Wait> waits);

  ActionPtr<AtWriteAction> CallCommand();
  void MakeRequest();
  UpdateStatus WaitResponses(TimePoint current_time);

  AtSupport* at_support_;
  Command at_command_;
  std::list<WaitObserver> wait_observers_;
  WaitObserver error_observer_;
  TimePoint start_time_;
  std::size_t response_count_;
  StateMachine<State> state_;
  Subscription command_sub_;
  MultiSubscription wait_response_sub_;
};
}  // namespace ae

#endif  // AETHER_SERIAL_PORTS_AT_SUPPORT_AT_REQUEST_H_
