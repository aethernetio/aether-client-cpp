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

#ifndef AETHER_CHANNELS_CHANNEL_H_
#define AETHER_CHANNELS_CHANNEL_H_

#include "aether/obj/obj.h"
#include "aether/types/address.h"
#include "aether/adapters/adapter.h"
#include "aether/channels/channels_types.h"
#include "aether/channels/channel_statistics.h"

namespace ae {
class Channel : public Obj {
  AE_OBJECT(Channel, Obj, 0)

  Channel() = default;

 public:
  Channel(Adapter::ptr adapter, Domain* domain);

  AE_OBJECT_REFLECT(AE_MMBRS(address, adapter_, transport_properties_,
                             channel_statistics_))

  /**
   * \brief Make transport from this channel.
   */
  ActionPtr<TransportBuilderAction> TransportBuilder();

  ChannelTransportProperties const& transport_properties() const;
  ChannelStatistics& channel_statistics();

  UnifiedAddress address;

 private:
  Adapter::ptr adapter_;
  ChannelTransportProperties transport_properties_;
  ChannelStatistics::ptr channel_statistics_;
};

}  // namespace ae

#endif  // AETHER_CHANNELS_CHANNEL_H_
