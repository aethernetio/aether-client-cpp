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

#ifndef AETHER_CHANNELS_WIFI_CHANNEL_H_
#define AETHER_CHANNELS_WIFI_CHANNEL_H_

#include "aether/types/address.h"
#include "aether/channels/channel.h"
#include "aether/access_points/wifi_access_point.h"

namespace ae {
class Aether;
class IPoller;
class DnsResolver;

class WifiChannel final : public Channel {
  AE_OBJECT(WifiChannel, Channel, 0)
 public:
  WifiChannel(Aether& aether, IPoller& poller, DnsResolver& resolver,
              WifiAccessPoint& access_point, Endpoint address, Domain* domain);

  Duration TransportBuildTimeout() const override;

  ActionPtr<TransportBuilderAction> TransportBuilder() override;

  Endpoint address;

 private:
  Aether* aether_;
  IPoller* poller_;
  DnsResolver* resolver_;
  WifiAccessPoint* access_point_;
};
}  // namespace ae

#endif  // AETHER_CHANNELS_WIFI_CHANNEL_H_
