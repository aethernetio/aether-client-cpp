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

#include "aether/types/uid.h"
#include "aether/ptr/ptr_view.h"
#include "aether/actions/action_context.h"

#include "aether/stream_api/buffer_stream.h"

#include "aether/client_connections/cloud_connection.h"
#include "aether/connection_manager/client_cloud_manager.h"
#include "aether/connection_manager/client_connection_manager.h"

namespace ae {
class Client;
class Cloud;
namespace p2p_stream_internal {
class MessageSendStream;
class ReadMessageGate;
}  // namespace p2p_stream_internal

class P2pStream final : public ByteIStream {
 public:
  P2pStream(ActionContext action_context, ObjPtr<Client> const& client,
            Uid destination);

  ~P2pStream() override;

  AE_CLASS_NO_COPY_MOVE(P2pStream);

  ActionPtr<StreamWriteAction> Write(DataBuffer&& data) override;

  StreamUpdateEvent::Subscriber stream_update_event() override;

  StreamInfo stream_info() const override;

  OutDataEvent::Subscriber out_data_event() override;

  void Restream() override;

  void WriteOut(DataBuffer const& data);

  Uid destination() const;

 private:
  void ConnectReceive();
  void ConnectSend();

  void DataReceived(AeMessage const& data);
  std::unique_ptr<ClientConnectionManager> MakeConnectionManager(
      ObjPtr<Cloud> const& cloud);
  std::unique_ptr<CloudConnection> MakeDestinationStream(
      ClientConnectionManager& connection_manager);

  ActionContext action_context_;
  PtrView<Client> client_;
  Uid destination_;

  // connection manager to destination cloud
  std::unique_ptr<ClientConnectionManager> destination_connection_manager_;
  std::unique_ptr<CloudConnection> destination_cloud_connection_;
  BufferStream<AeMessage> buffer_stream_;
  std::unique_ptr<p2p_stream_internal::MessageSendStream> message_send_stream_;

  std::unique_ptr<p2p_stream_internal::ReadMessageGate> read_message_gate_;
  OutDataEvent out_data_event_;

  Subscription get_client_connection_sub_;
};

}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_P2P_MESSAGE_STREAM_H_
