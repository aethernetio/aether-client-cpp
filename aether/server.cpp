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
#include <cassert>

#include "aether/tele/tele.h"

namespace ae {
Server::Server(ServerId server_id,
               std::shared_ptr<AdapterRegistry> adapter_registry,
               Domain* domain)
    : Obj{domain},
      server_id{server_id},
      adapter_registry_{std::move(adapter_registry)} {
  assert((server_id != 0) && "Server ID must be non-zero");
  domain_->Load(*this, Hash(kTypeName, server_id));
  AE_TELED_DEBUG("Loaded server id={}, endpoints={}", server_id, endpoints);
  Register();
}

Server::Server(ServerId server_id, std::vector<Endpoint> endpoints,
               std::shared_ptr<AdapterRegistry> adapter_registry,
               Domain* domain)
    : Obj{domain},
      server_id{server_id},
      endpoints{std::move(endpoints)},
      adapter_registry_{std::move(adapter_registry)} {
  Register();
  domain_->Save(*this, Hash(kTypeName, server_id));
}

Server::ChannelsChanged::Subscriber Server::channels_changed() {
  return EventSubscriber{channels_changed_};
}

void Server::Register() {
  UpdateSubscription();

  for (auto const& adapter : adapter_registry_->adapters()) {
    for (auto* ap : adapter->access_points()) {
      AddChannels(*ap);
    }
  }
}

void Server::UpdateSubscription() {
  for (auto const& adapter : adapter_registry_->adapters()) {
    access_point_added_.Push(adapter->new_access_point().Subscribe(
        MethodPtr<&Server::AddChannels>{this}));
  }
}

void Server::AddChannels(AccessPoint& access_point) {
  auto new_channels = access_point.GenerateChannels(*this);
  channels.reserve(channels.size() + new_channels.size());
  for (auto& ch : new_channels) {
    channels.emplace_back(std::move(ch));
  }
  channels_changed_.Emit();
}

}  // namespace ae
