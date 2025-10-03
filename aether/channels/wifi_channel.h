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

#include "aether/channels/channel.h"
#include "aether/access_points/wifi_access_point.h"

namespace ae {
class Aether;
class IPoller;
class DnsResolver;

class WifiChannel final : public Channel {
  AE_OBJECT(WifiChannel, Channel, 0)
  WifiChannel() = default;

 public:
  WifiChannel(ObjPtr<Aether> aether, ObjPtr<IPoller> poller,
              ObjPtr<DnsResolver> resolver, WifiAccessPoint::ptr access_point,
              UnifiedAddress address, Domain* domain);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, poller_, resolver_, access_point_))

  Duration TransportBuildTimeout() const override;

  ActionPtr<TransportBuilderAction> TransportBuilder() override;

 private:
  Obj::ptr aether_;
  Obj::ptr poller_;
  Obj::ptr resolver_;
  WifiAccessPoint::ptr access_point_;
};
}  // namespace ae

#endif  // AETHER_CHANNELS_WIFI_CHANNEL_H_
