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

#ifndef AETHER_SERVER_CONNECTIONS_CLIENT_SERVER_CONNECTION_H_
#define AETHER_SERVER_CONNECTIONS_CLIENT_SERVER_CONNECTION_H_

#include "aether/actions/action_context.h"

#include "aether/common.h"
#include "aether/ae_actions/ping.h"
#include "aether/actions/action_ptr.h"
#include "aether/crypto/icrypto_provider.h"

#include "aether/work_cloud_api/work_server_api/login_api.h"
#include "aether/work_cloud_api/client_api/client_api_safe.h"
#include "aether/work_cloud_api/client_api/client_api_unsafe.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

#include "aether/server_connections/server_connection.h"

namespace ae {
class Client;
class Server;
class Channel;

/**
 * \brief Client's connection to a server for messages send.
 */
class ClientServerConnection {
 public:
  ClientServerConnection(ActionContext action_context,
                         Ptr<Client> const& client, Ptr<Server> const& server);
  ~ClientServerConnection();

  AE_CLASS_NO_COPY_MOVE(ClientServerConnection)

  void Restream();
  StreamInfo stream_info() const;
  ByteIStream::StreamUpdateEvent::Subscriber stream_update_event();

  ActionPtr<WriteAction> AuthorizedApiCall(SubApi<AuthorizedApi> auth_api);
  ClientApiSafe& client_safe_api();

  ServerConnection& server_connection();

 private:
  void OutData(DataBuffer const& data);
  void ChannelChanged();

  ActionContext action_context_;
  PtrView<Server> server_;
  Uid ephemeral_uid_;

  std::unique_ptr<ICryptoProvider> crypto_provider_;
  ProtocolContext protocol_context_;
  ClientApiUnsafe client_api_unsafe_;
  LoginApi login_api_;

  ServerConnection server_connection_;
  OwnActionPtr<Ping> ping_;

  Subscription ping_sub_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_CLIENT_SERVER_CONNECTION_H_
