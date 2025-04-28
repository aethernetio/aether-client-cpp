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

#include "aether/memory.h"
#include "aether/ptr/ptr_view.h"
#include "aether/events/events.h"
#include "aether/actions/action_context.h"

#include "aether/stream_api/istream.h"
#include "aether/stream_api/debug_gate.h"
#include "aether/stream_api/tied_stream.h"
#include "aether/stream_api/crypto_stream.h"
#include "aether/stream_api/protocol_stream.h"
#include "aether/stream_api/event_subscribe_gate.h"

#include "aether/methods/client_api/client_root_api.h"
#include "aether/methods/client_api/client_safe_api.h"
#include "aether/methods/work_server_api/login_api.h"
#include "aether/methods/work_server_api/authorized_api.h"

namespace ae {
class Client;

class ClientToServerStream : public ByteStream {
 public:
  ClientToServerStream(ActionContext action_context, Ptr<Client> const& client,
                       ServerId server_id,
                       std::unique_ptr<ByteStream> server_stream);

  ~ClientToServerStream() override;

  AE_CLASS_NO_COPY_MOVE(ClientToServerStream);

  InGate& in() override;
  void LinkOut(OutGate& out) override;

  ProtocolContext& protocol_context();
  ClientSafeApi& client_safe_api();
  AuthorizedApi& authorized_api();

 private:
  ActionContext action_context_;
  PtrView<Client> client_;
  ServerId server_id_;

  ProtocolContext protocol_context_;

  ClientRootApi client_root_api_;
  ClientSafeApi client_safe_api_;
  LoginApi login_api_;
  AuthorizedApi authorized_api_;

  Event<void()> connected_event_;
  Event<void()> connection_error_event_;

  // stream to the server with login api and encryption
  std::optional<TiedStream<
      ProtocolReadGate<ClientSafeApi&>, DebugGate, CryptoGate,
      ProtocolWriteMessageGate<DataBuffer>, EventSubscribeGate<DataBuffer>,
      ProtocolReadGate<ClientRootApi&>, std::unique_ptr<ByteStream>>>
      client_auth_stream_;

  Subscription connection_success_subscription_;
  Subscription connection_failed_subscription_;
  Subscription gate_update_subscription_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_CLIENT_TO_SERVER_STREAM_H_
