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

#ifndef AETHER_CHANNELS_ETHERNET_CHANNEL_H_
#define AETHER_CHANNELS_ETHERNET_CHANNEL_H_

#include "aether/types/address.h"
#include "aether/channels/channel.h"

namespace ae {
class Aether;
class IPoller;
class DnsResolver;

class EthernetChannel : public Channel {
  AE_OBJECT(EthernetChannel, Channel, 0)
 public:
  EthernetChannel() = default;

  EthernetChannel(ObjPtr<Aether> aether, ObjPtr<DnsResolver> dns_resolver,
                  ObjPtr<IPoller> poller, Endpoint address, Domain* domain);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, poller_, dns_resolver_, address))

  ActionPtr<TransportBuilderAction> TransportBuilder() override;

  Endpoint address;

 private:
  Obj::ptr aether_;
  Obj::ptr poller_;
  Obj::ptr dns_resolver_;
};
}  // namespace ae

#endif  // AETHER_CHANNELS_ETHERNET_CHANNEL_H_
