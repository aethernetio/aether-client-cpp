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

#include "aether/server.h"

#include <utility>

namespace ae {
Server::Server(ObjProp prop, ServerId server_id,
               std::vector<Endpoint> endpoints,
               AdapterRegistry::ptr adapter_registry)
    : Obj{prop},
      server_id{server_id},
      endpoints{std::move(endpoints)},
      adapter_registry_{std::move(adapter_registry)},
      subscribed_{} {
  Register();
}

void Server::Update(TimePoint current_time) {
  if (!subscribed_) {
    subscribed_ = true;
    UpdateSubscription();
  }
  update_time = current_time;
}

void Server::Register() {
  UpdateSubscription();

  for (auto const& adapter : adapter_registry_->adapters()) {
    for (auto const& ap : adapter->access_points()) {
      AddChannels(ap);
    }
  }
  subscribed_ = true;
}

Server::ChannelsChanged::Subscriber Server::channels_changed() {
  return EventSubscriber{channels_changed_};
}

void Server::UpdateSubscription() {
  assert(adapter_registry_.is_valid());
  adapter_registry_.WithLoaded([&](auto const& ar) {
    for (auto const& adapter : ar->adapters()) {
      access_point_added_ += adapter->new_access_point().Subscribe(
          MethodPtr<&Server::AddChannels>{this});
    }
  });
  channels_changed_.Emit();
}

void Server::AddChannels(AccessPoint::ptr const& access_point) {
  auto server_ptr = Server::ptr::MakeFromThis(this);
  auto new_channels = access_point->GenerateChannels(server_ptr);
  channels.insert(std::end(channels), std::begin(new_channels),
                  std::end(new_channels));
  channels_changed_.Emit();
}

}  // namespace ae
