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

#ifndef AETHER_SERVER_H_
#define AETHER_SERVER_H_

#include <vector>

#include "aether/types/address.h"
#include "aether/events/events.h"
#include "aether/types/server_id.h"
#include "aether/channels/channel.h"
#include "aether/adapter_registry.h"
#include "aether/events/multi_subscription.h"

namespace ae {
class Server : public Obj {
  AE_OBJECT(Server, Obj, 0)

  Server() = default;

 public:
  using ChannelsChanged = Event<void()>;

  explicit Server(ObjProp prop, ServerId server_id,
                  std::vector<Endpoint> endpoints,
                  AdapterRegistry::ptr adapter_registry);

  AE_OBJECT_REFLECT(AE_MMBRS(server_id, endpoints, adapter_registry_, channels))

  void Update(TimePoint current_time) override;

  ChannelsChanged::Subscriber channels_changed();

  ServerId server_id;
  std::vector<Endpoint> endpoints;
  std::vector<Channel::ptr> channels;

 private:
  void Register();
  void UpdateSubscription();
  void AddChannels(AccessPoint::ptr const& access_point);

  AdapterRegistry::ptr adapter_registry_;
  MultiSubscription access_point_added_;
  ChannelsChanged channels_changed_;
  bool subscribed_;
};
}  // namespace ae
#endif  // AETHER_SERVER_H_ */
