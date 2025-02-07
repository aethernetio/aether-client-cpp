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

#ifndef AETHER_TRANSPORT_ACTIONS_NAME_ADDRESS_CHANNEL_CONNECTION_H_
#define AETHER_TRANSPORT_ACTIONS_NAME_ADDRESS_CHANNEL_CONNECTION_H_

#include "aether/config.h"

#if AE_SUPPORT_CLOUD_DNS

#  include <vector>
#  include <optional>

#  include "aether/address.h"
#  include "aether/ptr/ptr.h"
#  include "aether/actions/action_context.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/dns/dns_resolve.h"

#  include "aether/transport/actions/channel_connection_action.h"
#  include "aether/transport/actions/ip_channel_connection.h"

namespace ae {
class Adapter;
class Aether;
class ApiClass;

class NameAddressChannelConnectionAction : public ChannelConnectionAction {
  enum class State : std::uint8_t {
    None,
    Start,
    TryConnection,
    Connected,
    NotConnected,
  };

 public:
  NameAddressChannelConnectionAction(ActionContext action_context,
                                     NameAddress name_address,
                                     Ptr<Aether> const& aether,
                                     Ptr<Adapter> const& adapter);

  ~NameAddressChannelConnectionAction() override;

  TimePoint Update(TimePoint current_time) override;
  Ptr<ITransport> transport() const override;
  ConnectionInfo connection_info() const override;

 private:
  void NameResolve(TimePoint current_time);
  void TryConnection(TimePoint current_time);

  NameAddress name_address_;
  Ptr<DnsResolver> dns_resolver_;
  ActionContext action_context_;
  PtrView<Adapter> adapter_;

  Ptr<ITransport> transport_;
  ConnectionInfo connection_info_;

  StateMachine<State> state_;
  std::vector<IpAddressPortProtocol> ip_address_port_protocols_;
  std::vector<IpAddressPortProtocol>::iterator ip_address_port_protocol_it_;

  std::optional<IpAddressChannelConnectionAction>
      ip_address_channel_connection_;

  MultiSubscription dns_resolve_subscriptions_;
  MultiSubscription address_subscriptions_;
  Subscription state_changed_subscription_;
};

}  // namespace ae

#endif
#endif  // AETHER_TRANSPORT_ACTIONS_NAME_ADDRESS_CHANNEL_CONNECTION_H_
