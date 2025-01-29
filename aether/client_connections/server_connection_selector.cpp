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

#include "aether/client_connections/server_connection_selector.h"

#include <utility>

#include "aether/server_list/no_filter_server_list_policy.h"

namespace ae {
ServerConnectionSelector::ServerConnectionSelector(
    Ptr<Cloud> cloud,
    Ptr<IServerConnectionFactory> client_server_connection_factory)
    : server_list_{MakePtr<ServerList>(MakePtr<NoFilterServerListPolicy>(),
                                       std::move(cloud))},
      server_connection_factory_{std::move(client_server_connection_factory)} {}

void ServerConnectionSelector::Init() { server_list_->Init(); }

void ServerConnectionSelector::Next() { server_list_->Next(); }

Ptr<ClientServerConnection> ServerConnectionSelector::GetConnection() {
  auto item = server_list_->Get();
  return server_connection_factory_->CreateConnection(item.server(),
                                                      item.channel());
}

bool ServerConnectionSelector::End() { return server_list_->End(); }

}  // namespace ae
