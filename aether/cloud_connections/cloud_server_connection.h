/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_CLOUD_CONNECTIONS_CLOUD_SERVER_CONNECTION_H_
#define AETHER_CLOUD_CONNECTIONS_CLOUD_SERVER_CONNECTION_H_

#include <cstddef>
#include <memory>

#include "aether-objects/ptr/ptr_view.h"

#include "aether/cloud.h"
#include "aether/server.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/server_connections/iserver_connection_factory.h"

namespace ae {
class CloudServerConnection {
 public:
  CloudServerConnection(Ptr<Cloud> const& cloud, ServerId server_id,
                        IServerConnectionFactory& connection_factory);

  void Restream();

  bool quarantine() const;
  void SetQuarantine(bool value);

  bool Connect();
  void Disconnect();

  ClientServerConnection* client_connection();

  // The reference is valid only while the owning Cloud and server-map entry
  // remain unchanged.
  Server::ptr const& server() const;
  ServerId server_id() const { return server_id_; }
  std::size_t priority() const;
  void SetPriority(std::size_t priority);

 private:
  CloudServer& cloud_server() const;

  PtrView<Cloud> cloud_;
  ServerId server_id_;
  IServerConnectionFactory* connection_factory_;
  std::shared_ptr<ClientServerConnection> client_connection_;
  bool is_quarantined_ = false;
};
}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_CLOUD_SERVER_CONNECTION_H_
