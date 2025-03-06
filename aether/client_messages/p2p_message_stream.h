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

#ifndef AETHER_CLIENT_MESSAGES_P2P_MESSAGE_STREAM_H_
#define AETHER_CLIENT_MESSAGES_P2P_MESSAGE_STREAM_H_

#include "aether/common.h"

#include "aether/uid.h"
#include "aether/memory.h"
#include "aether/client.h"
#include "aether/ptr/ptr_view.h"
#include "aether/actions/action_context.h"

#include "aether/stream_api/istream.h"
#include "aether/stream_api/buffer_gate.h"
#include "aether/stream_api/unidirectional_gate.h"

#include "aether/client_connections/client_connection.h"

namespace ae {
class P2pStream final : public ByteStream {
 public:
  P2pStream(ActionContext action_context, Ptr<Client> const& client,
            Uid destination, StreamId stream_id);
  P2pStream(ActionContext action_context, Ptr<Client> const& client,
            Uid destination, StreamId stream_id,
            std::unique_ptr<ByteStream> receive_stream);

  ~P2pStream() override;

  AE_CLASS_NO_COPY_MOVE(P2pStream);

  InGate& in() override;

  void LinkOut(OutGate& /* out */) override;

 private:
  void ConnectReceive();
  void ConnectSend();
  void TieSendStream(ClientConnection& client_connection);

  void GetReceiveStream();
  void GetSendStream();

  ActionContext action_context_;
  PtrView<Client> client_;
  Uid destination_;
  StreamId stream_id_;

  Ptr<ClientConnection> receive_client_connection_;
  Ptr<ClientConnection> send_client_connection_;

  BufferGate buffer_gate_;
  ParallelGate<WriteOnlyGate, ReadOnlyGate> send_receive_gate_;
  std::unique_ptr<ByteStream> receive_stream_;
  std::unique_ptr<ByteStream> send_stream_;

  Subscription get_client_connection_subscription_;
};

}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_P2P_MESSAGE_STREAM_H_
