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

#include "aether/cloud.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <map>
#include <utility>

namespace ae {

Cloud::Cloud(ObjProp prop) : Obj{prop} {}

void Cloud::AddServer(Server::ptr server) {
  [[maybe_unused]] auto const server_loaded =
      server.WithLoaded([&](auto const& loaded_server) {
        auto const server_id = loaded_server->server_id;
        server.SetFlags(ObjFlags::kUnloadedByDefault);
        auto it = servers_.find(server_id);
        if (it != servers_.end()) {
          it->second.server = std::move(server);
          return;
        }

        assert(servers_.size() < std::numeric_limits<std::uint16_t>::max() &&
               "cloud server priority exceeds persisted capacity");
        auto priority = std::uint16_t{0};
        if (!servers_.empty()) {
          auto const last_server = std::max_element(
              servers_.begin(), servers_.end(),
              [](auto const& left, auto const& right) {
                return left.second.priority < right.second.priority;
              });
          assert(last_server->second.priority <
                     std::numeric_limits<std::uint16_t>::max() &&
                 "cloud server priority exceeds persisted capacity");
          priority =
              static_cast<std::uint16_t>(last_server->second.priority + 1);
        }
        servers_.emplace(server_id, CloudServer{priority, std::move(server)});
      });
  assert(server_loaded && "cloud server must load");
  cloud_updated_.Emit();
}

void Cloud::SetServers(std::vector<Server::ptr> const& servers) {
  assert(servers.size() < std::numeric_limits<std::uint16_t>::max() &&
         "cloud server priority exceeds persisted capacity");
  servers_.clear();
  std::uint16_t priority = 0;
  for (auto const& server : servers) {
    auto stored_server = server;
    stored_server.SetFlags(ObjFlags::kUnloadedByDefault);
    auto const server_id = stored_server.WithLoaded(
        [](auto const& loaded_server) { return loaded_server->server_id; });
    assert(server_id && "cloud server must load");
    if (!server_id) {
      continue;
    }
    [[maybe_unused]] auto const inserted =
        servers_
            .insert_or_assign(*server_id,
                              CloudServer{priority++, std::move(stored_server)})
            .second;
    assert(inserted && "cloud server must not duplicate");
  }
  cloud_updated_.Emit();
}

std::map<ServerId, CloudServer>& Cloud::servers() { return servers_; }

std::map<ServerId, CloudServer> const& Cloud::servers() const {
  return servers_;
}

EventSubscriber<void()> Cloud::cloud_updated() {
  return EventSubscriber{cloud_updated_};
}

}  // namespace ae
