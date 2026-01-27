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

Cloud::Cloud(ObjProp prop) : Obj{prop} {}

void Cloud::AddServer(Server::ptr server) {
  server.SetFlags(ObjFlags::kUnloadedByDefault);
  servers_.emplace_back(std::move(server));
  cloud_updated_.Emit();
}

void Cloud::SetServers(std::vector<Server::ptr> servers) {
  for (auto&& s : std::move(servers)) {
    s.SetFlags(ObjFlags::kUnloadedByDefault);
    servers_.emplace_back(std::move(s));
  }
  cloud_updated_.Emit();
}

std::vector<Server::ptr>& Cloud::servers() { return servers_; }

EventSubscriber<void()> Cloud::cloud_updated() {
  return EventSubscriber{cloud_updated_};
}

}  // namespace ae
