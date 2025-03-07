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

#include "aether/adapters/ethernet.h"

#include <utility>

#include "aether/aether.h"

#include "aether/tele/tele.h"

#include "aether/transport/low_level/tcp/unix_tcp.h"
#include "aether/transport/low_level/tcp/win_tcp.h"
#include "aether/transport/low_level/tcp/lwip_tcp.h"

namespace ae {

EthernetAdapter::EthernetCreateTransportAction::EthernetCreateTransportAction(
    ActionContext action_context, std::unique_ptr<ITransport> transport)
    : CreateTransportAction{action_context},
      transport_{std::move(transport)},
      once_{true} {}

TimePoint EthernetAdapter::EthernetCreateTransportAction::Update(
    TimePoint current_time) {
  if (transport_ && once_) {
    once_ = false;
    Action::Result(*this);
  }
  return current_time;
}

std::unique_ptr<ITransport>
EthernetAdapter::EthernetCreateTransportAction::transport() {
  return std::move(transport_);
}

#ifdef AE_DISTILLATION
EthernetAdapter::EthernetAdapter(Aether::ptr aether, IPoller::ptr poller,
                                 Domain* domain)
    : Adapter(domain), aether_{std::move(aether)}, poller_{std::move(poller)} {}
#endif  // AE_DISTILLATION

ActionView<CreateTransportAction> EthernetAdapter::CreateTransport(
    IpAddressPortProtocol const& address_port_protocol) {
  if (!create_transport_actions_) {
    create_transport_actions_.emplace(
        ActionContext{*aether_.as<Aether>()->action_processor});
  }

  CleanDeadTransports();
  auto transport = FindInCache(address_port_protocol);
  if (!transport) {
#if defined UNIX_TCP_TRANSPORT_ENABLED
    assert(address_port_protocol.protocol == Protocol::kTcp);
    transport =
        make_unique<UnixTcpTransport>(*aether_.as<Aether>()->action_processor,
                                      poller_, address_port_protocol);
#elif defined LWIP_TCP_TRANSPORT_ENABLED
    assert(address_port_protocol.protocol == Protocol::kTcp);
    transport =
        make_unique<LwipTcpTransport>(*aether_.as<Aether>()->action_processor,
                                      poller_, address_port_protocol);
#elif defined WIN_TCP_TRANSPORT_ENABLED
    assert(address_port_protocol.protocol == Protocol::kTcp);
    transport =
        make_unique<WinTcpTransport>(*aether_.as<Aether>()->action_processor,
                                     poller_, address_port_protocol);
#else
    return {};
#endif
    AddToCache(address_port_protocol, *transport);
  } else {
    AE_TELED_DEBUG("Got transport from cache");
  }

  return create_transport_actions_->Emplace(std::move(transport));
}

}  // namespace ae
