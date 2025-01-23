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

#include "aether/adapters/register_wifi.h"

#include "aether/aether.h"

namespace ae {

#ifdef AE_DISTILLATION
RegisterWifiAdapter::RegisterWifiAdapter(ObjPtr<Aether> aether,
                                         IPoller::ptr poller, std::string ssid,
                                         std::string pass, Domain* domain)
    : ParentWifiAdapter{std::move(aether), std::move(poller), std::move(ssid),
                        std::move(pass), domain},
      ethernet_adapter_{domain->CreateObj<EthernetAdapter>(aether_, poller_)} {}
#endif  // AE_DISTILLATION

ActionView<CreateTransportAction> RegisterWifiAdapter::CreateTransport(
    IpAddressPortProtocol const& address_port_protocol) {
  return ethernet_adapter_->CreateTransport(address_port_protocol);
}

}  // namespace ae
