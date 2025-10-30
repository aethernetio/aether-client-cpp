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
// #include "aether/memory.h"
#include "aether/ae_actions/ping.h"
#include "aether/actions/action_ptr.h"
#include "aether/crypto/icrypto_provider.h"
#include "aether/ae_actions/telemetry.h"
#include "aether/stream_api/buffer_stream.h"
#include "aether/server_connections/server_channel.h"
#include "aether/server_connections/channel_manager.h"
#include "aether/server_connections/channel_selection_stream.h"

#include "aether/work_cloud_api/work_server_api/login_api.h"
#include "aether/work_cloud_api/client_api/client_api_safe.h"
#include "aether/work_cloud_api/client_api/client_api_unsafe.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

namespace ae {
class Aether;
class Client;
class Server;
class Channel;

/**
 * \brief Client's connection to a server for messages send.
 */
class ClientServerConnection {
 public:
  explicit ClientServerConnection(ActionContext action_context,
                                  ObjPtr<Aether> const& aether,
                                  ObjPtr<Client> const& client,
                                  ObjPtr<Server> const& server);

  AE_CLASS_NO_COPY_MOVE(ClientServerConnection)

  ByteIStream& stream();
  void Restream();
  ActionPtr<StreamWriteAction> AuthorizedApiCall(
      SubApi<AuthorizedApi> auth_api);
  ClientApiSafe& client_safe_api();

  void SendTelemetry();

 private:
  void OutData(DataBuffer const& data);
  void SubscribeToSelectChannel();
  void StreamUpdate();

  ActionContext action_context_;
  PtrView<Client> client_;
  PtrView<Server> server_;

  std::unique_ptr<ICryptoProvider> crypto_provider_;
  ProtocolContext protocol_context_;
  ClientApiUnsafe client_api_unsafe_;
  LoginApi login_api_;

  ChannelManager channel_manager_;

  ChannelSelectStream channel_select_stream_;
  BufferStream<DataBuffer> buffer_stream_;

  OwnActionPtr<Ping> ping_;
#if defined TELEMETRY_ENABLED
  OwnActionPtr<Telemetry> telemetry_;
#endif

  ServerChannel const* server_channel_;

  Subscription out_data_sub_;
  Subscription stream_update_sub_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_CLIENT_SERVER_CONNECTION_H_
