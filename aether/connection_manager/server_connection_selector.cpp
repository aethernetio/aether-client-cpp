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

#include "aether/connection_manager/server_connection_selector.h"

#include <utility>
#include <algorithm>

#include "aether/cloud.h"
#include "aether/server.h"

namespace ae {
ServerConnectionSelector::ServerConnectionSelector(
    ObjPtr<Cloud> const& cloud, IServerPriorityPolicy& server_priority_policy,
    std::unique_ptr<IServerConnectionFactory> client_server_connection_factory)
    : server_connection_factory_{std::move(client_server_connection_factory)} {
  // TODO: add server priority
  auto srvs = cloud->servers();
  // filter servers
  srvs.erase(std::remove_if(std::begin(srvs), std::end(srvs),
                            [&](auto const& server) {
                              return server_priority_policy.Filter(server);
                            }),
             std::end(srvs));
  // sort servers
  std::sort(std::begin(srvs), std::end(srvs),
            [&](auto const& left, auto const& right) {
              return server_priority_policy.Compare(left, right);
            });
  // copy to servers_ list
  servers_.reserve(srvs.size());
  std::transform(std::begin(srvs), std::end(srvs), std::back_inserter(servers_),
                 [](auto const& server) -> PtrView<Server> { return server; });
}

void ServerConnectionSelector::Init() {
  current_server_ = std::begin(servers_);
}

void ServerConnectionSelector::Next() { ++current_server_; }

RcPtr<ClientServerConnection> ServerConnectionSelector::Get() {
  auto server = current_server_->Lock();
  if (!server) {
    current_server_ = std::end(servers_);
    return nullptr;
  }
  return server_connection_factory_->CreateConnection(server);
}

bool ServerConnectionSelector::End() {
  return current_server_ == std::end(servers_);
}

}  // namespace ae
