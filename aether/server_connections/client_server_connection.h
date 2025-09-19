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

#include "aether/events/events.h"
#include "aether/stream_api/istream.h"
#include "aether/actions/action_context.h"

#include "aether/common.h"
// #include "aether/memory.h"
#include "aether/ae_actions/ping.h"
#include "aether/actions/action_ptr.h"
// #include "aether/ae_actions/telemetry.h"
#include "aether/server_connections/server_channel.h"
#include "aether/server_connections/channel_manager.h"
#include "aether/server_connections/client_to_server_stream.h"

namespace ae {
class Aether;
class Client;
class Server;
class Channel;

/**
 * \brief Client's connection to a server for messages send.
 */
class ClientServerConnection {
  class ChannelSelectStream final : public ByteIStream {
   public:
    explicit ChannelSelectStream(
        ClientServerConnection& client_server_connection);

    ActionPtr<StreamWriteAction> Write(DataBuffer&& data) override;
    StreamInfo stream_info() const override;
    StreamUpdateEvent::Subscriber stream_update_event() override;
    OutDataEvent::Subscriber out_data_event() override;

    ServerChannel const* server_channel() const;

    // Mark selected channel as failed
    void ChannelFailed();

   private:
    void SelectChannel();
    void StreamUpdate();

    ClientServerConnection* client_server_connection_;
    ServerChannel* server_channel_;
    StreamInfo stream_info_;
    OutDataEvent out_data_event_;
    StreamUpdateEvent stream_update_event_;

    Subscription stream_update_sub_;
    Subscription out_data_sub_;
  };

 public:
  explicit ClientServerConnection(ActionContext action_context,
                                  ObjPtr<Aether> const& aether,
                                  ObjPtr<Client> const& client,
                                  ObjPtr<Server> const& server);

  AE_CLASS_NO_COPY_MOVE(ClientServerConnection)

  ClientToServerStream& server_stream();

 private:
  void SubscribeToSelectChannel();
  void StreamUpdate();

  ActionContext action_context_;
  PtrView<Client> client_;
  PtrView<Server> server_;
  OwnActionPtr<Ping> ping_;
  // #if defined TELEMETRY_ENABLED
  //   OwnActionPtr<Telemetry> telemetry_;
  // #endif
  ChannelManager channel_manager_;
  ServerChannel const* server_channel_;
  ClientToServerStream client_to_server_stream_;
  ChannelSelectStream channel_select_stream_;

  Subscription stream_update_sub_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_CLIENT_SERVER_CONNECTION_H_
