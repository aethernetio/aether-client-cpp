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

namespace ae {

Cloud::Cloud(Domain* domain) : Obj{domain} {}

void Cloud::AddServer(Server::ptr const& server) {
  servers_.push_back(server);
  servers_.back().SetFlags(ObjFlags::kUnloadedByDefault);
  cloud_updated_.Emit();
}

void Cloud::AddServers(std::vector<Server::ptr> const& servers) {
  for (auto const& s : servers) {
    servers_.push_back(s);
    servers_.back().SetFlags(ObjFlags::kUnloadedByDefault);
  }
  cloud_updated_.Emit();
}

void Cloud::LoadServer(Server::ptr& server) {
  if (!server) {
    domain_->LoadRoot(server);
    assert(server);
  }
}

std::vector<Server::ptr>& Cloud::servers() { return servers_; }

EventSubscriber<void()> Cloud::cloud_updated() {
  return EventSubscriber{cloud_updated_};
}

}  // namespace ae
