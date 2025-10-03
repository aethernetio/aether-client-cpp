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
Server::Server(ServerId server_id, std::vector<UnifiedAddress> endpoints,
               Domain* domain)
    : Obj{domain},
      server_id{server_id},
      endpoints{std::move(endpoints)},
      subscribed_{} {}

void Server::Update(TimePoint current_time) {
  if (!subscribed_) {
    subscribed_ = true;
    UpdateSubscription();
  }
  update_time_ = current_time;
}

void Server::Register(AdapterRegistry::ptr adapter_registry) {
  adapter_registry_ = std::move(adapter_registry);
  UpdateSubscription();

  for (auto const& adapter : adapter_registry_->adapters()) {
    for (auto const& ap : adapter->access_points()) {
      AddChannels(ap);
    }
  }
  subscribed_ = true;
  channels_changed_.Emit();
}

Server::ChannelsChanged::Subscriber Server::channels_changed() {
  return EventSubscriber{channels_changed_};
}

void Server::UpdateSubscription() {
  if (!adapter_registry_) {
    domain_->LoadRoot(adapter_registry_);
  }
  assert(adapter_registry_);
  for (auto const& adapter : adapter_registry_->adapters()) {
    access_point_added_.Push(adapter->new_access_point().Subscribe(
        *this, MethodPtr<&Server::AddChannels>{}));
  }
}

void Server::AddChannels(AccessPoint::ptr const& access_point) {
  auto new_channels = access_point->GenerateChannels(endpoints);
  channels.insert(std::end(channels), std::begin(new_channels),
                  std::end(new_channels));
  channels_changed_.Emit();
}

}  // namespace ae
