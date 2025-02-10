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

#ifndef AETHER_CLIENT_CONNECTIONS_CLIENT_TO_SERVER_STREAM_H_
#define AETHER_CLIENT_CONNECTIONS_CLIENT_TO_SERVER_STREAM_H_

#include <optional>

#include "aether/events/events.h"
#include "aether/actions/action_context.h"

#include "aether/api_protocol/protocol_context.h"

#include "aether/stream_api/istream.h"
#include "aether/stream_api/stream_api.h"
#include "aether/stream_api/debug_gate.h"
#include "aether/stream_api/tied_stream.h"
#include "aether/stream_api/crypto_stream.h"
#include "aether/stream_api/protocol_stream.h"

#include "aether/methods/client_api/client_safe_api.h"

namespace ae {
class Client;

class ClientToServerStream : public ByteStream {
 public:
  ClientToServerStream(ActionContext action_context, Ptr<Client> client,
                       ServerId server_id, Ptr<ByteStream> server_stream);

  ~ClientToServerStream() override;

  AE_CLASS_NO_COPY_MOVE(ClientToServerStream);

  InGate& in() override;
  void LinkOut(OutGate& out) override;

 private:
  void OnConnected();
  void OnDisconnected();

  ActionContext action_context_;
  Ptr<Client> client_;
  ServerId server_id_;

  ProtocolContext protocol_context_;

  Event<void()> connected_event_;
  Event<void()> connection_error_event_;

  // stream to the server with login api and encryption
  std::optional<
      TiedStream<DebugGate, CryptoGate, StreamApiGate, ProtocolWriteGate,
                 ProtocolReadGate<ClientSafeApi>, Ptr<ByteStream>>>
      client_auth_stream_;

  Subscription connection_success_subscription_;
  Subscription connection_failed_subscription_;
  Subscription gate_update_subscription_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_CLIENT_TO_SERVER_STREAM_H_
