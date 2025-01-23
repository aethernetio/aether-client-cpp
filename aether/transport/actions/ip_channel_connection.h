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

#ifndef AETHER_TRANSPORT_ACTIONS_IP_CHANNEL_CONNECTION_H_
#define AETHER_TRANSPORT_ACTIONS_IP_CHANNEL_CONNECTION_H_

#include "aether/address.h"
#include "aether/state_machine.h"
#include "aether/actions/action_context.h"
#include "aether/events/multi_subscription.h"

#include "aether/transport/actions/channel_connection_action.h"

namespace ae {
class ApiClass;
class Adapter;

class IpAddressChannelConnectionAction : public ChannelConnectionAction {
  enum class State : std::uint8_t {
    None,
    Start,
    Connected,
    Failed,
  };

 public:
  IpAddressChannelConnectionAction(
      ActionContext action_context,
      IpAddressPortProtocol const& ip_address_port_protocol, Adapter& adapter);

  ~IpAddressChannelConnectionAction() override;

  TimePoint Update(TimePoint current_time) override;

  Ptr<ITransport> transport() const override;
  ConnectionInfo connection_info() const override;

 private:
  void TryConnect(TimePoint current_time);
  void TransportCreated(Ptr<ITransport> transport);

  IpAddressPortProtocol ip_address_port_protocol_;
  Adapter& adapter_;

  Ptr<ITransport> transport_;
  ConnectionInfo connection_info_;
  StateMachine<State> state_;

  MultiSubscription subscriptions_;
  Subscription state_changed_subscription_;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_ACTIONS_IP_CHANNEL_CONNECTION_H_
