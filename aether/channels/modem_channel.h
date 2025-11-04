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

#ifndef AETHER_CHANNELS_MODEM_CHANNEL_H_
#define AETHER_CHANNELS_MODEM_CHANNEL_H_

#include "aether/config.h"
#if AE_SUPPORT_MODEMS

#  include "aether/channels/channel.h"
#  include "aether/access_points/modem_access_point.h"

namespace ae {
class Aether;

class ModemChannel final : public Channel {
  AE_OBJECT(ModemChannel, Channel, 0)
  ModemChannel() = default;

 public:
  ModemChannel(ObjPtr<Aether> aether, ModemAccessPoint::ptr access_point,
               UnifiedAddress address, Domain* domain);

  AE_OBJECT_REFLECT(AE_MMBRS(access_point_))

  ActionPtr<TransportBuilderAction> TransportBuilder() override;

  Duration TransportBuildTimeout() const override;

 private:
  Obj::ptr aether_;
  ModemAccessPoint::ptr access_point_;
};
}  // namespace ae
#endif
#endif  // AETHER_CHANNELS_MODEM_CHANNEL_H_
