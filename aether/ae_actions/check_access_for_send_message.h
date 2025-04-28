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

#ifndef AETHER_AE_ACTIONS_CHECK_ACCESS_FOR_SEND_MESSAGE_H_
#define AETHER_AE_ACTIONS_CHECK_ACCESS_FOR_SEND_MESSAGE_H_

#include <cstdint>

#include "aether/common.h"
#include "aether/state_machine.h"
#include "aether/actions/action.h"

#include "aether/client_connections/client_to_server_stream.h"

namespace ae {
class CheckAccessForSendMessage final
    : public Action<CheckAccessForSendMessage> {
  static constexpr std::uint8_t kMaxRequestRepeatCount = 5;
  static constexpr Duration kRequestTimeout = std::chrono::seconds{1};

  enum class State : std::uint8_t {
    kSendRequest,
    kWaitResponse,
    kReceivedSuccess,
    kReceivedError,
    kSendError,
    kTimeout,
  };

 public:
  CheckAccessForSendMessage(ActionContext action_context,
                            ClientToServerStream& client_to_server_stream,
                            Uid destination);

  AE_CLASS_NO_COPY_MOVE(CheckAccessForSendMessage)

  TimePoint Update(TimePoint current_time) override;

  State state() const { return state_.get(); }

 private:
  void SendRequest(TimePoint current_time);
  TimePoint WaitResponse(TimePoint current_time);
  void ResponseReceived();
  void ErrorReceived();
  void SendError();

  ClientToServerStream* client_to_server_stream_;
  Uid destination_;

  std::uint8_t repeat_count_;
  TimePoint last_request_time_;
  StateMachine<State> state_;
  Subscription wait_check_success_;
  Subscription wait_check_error_;
  Subscription state_changed_;
  Subscription send_error_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_CHECK_ACCESS_FOR_SEND_MESSAGE_H_
