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

#ifndef AETHER_AE_ACTIONS_PING_H_
#define AETHER_AE_ACTIONS_PING_H_

#include <cstdint>

#include "aether/common.h"
#include "aether/server.h"
#include "aether/channel.h"
#include "aether/ptr/ptr.h"
#include "aether/state_machine.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"
#include "aether/stream_api/istream.h"
#include "aether/stream_api/protocol_stream.h"
#include "aether/api_protocol/protocol_context.h"
#include "aether/methods/client_api/client_safe_api.h"

namespace ae {
class Ping : public Action<Ping> {
  enum class State : std::uint8_t {
    kWaitLink,
    kSendPing,
    kWaitResponse,
    kWaitInterval,
    kError,
  };

 public:
  Ping(ActionContext action_context, Server::ptr const& server,
       Channel::ptr const& channel, ByteStream& server_stream,
       Duration ping_interval);
  ~Ping() override;

  AE_CLASS_NO_COPY_MOVE(Ping);

  TimePoint Update(TimePoint current_time) override;

 private:
  void SendPing(TimePoint current_time);
  void PingResponse();

  PtrView<Server> server_;
  PtrView<Channel> channel_;
  ByteStream* server_stream_;
  Duration ping_interval_;

  ProtocolContext protocol_context_;
  ClientSafeApi client_safe_api_;
  ProtocolReadGate<ClientSafeApi> read_client_safe_api_gate_;

  TimePoint last_ping_time_;
  Subscription write_subscription_;
  StateMachine<State> state_;
  Subscription state_changed_sub_;
  Subscription stream_changed_sub_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_PING_H_
