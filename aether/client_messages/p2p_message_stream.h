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

#include "aether/write_action/buffer_write.h"

#include "aether/cloud_connections/cloud_server_connections.h"
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
  P2pStream(ActionContext action_context, Ptr<Client> const& client,
            Uid destination);

  ~P2pStream() override;

  AE_CLASS_NO_COPY_MOVE(P2pStream);

  ActionPtr<WriteAction> Write(DataBuffer&& data) override;

  StreamUpdateEvent::Subscriber stream_update_event() override;

  StreamInfo stream_info() const override;

  OutDataEvent::Subscriber out_data_event() override;

  void Restream() override;

  void WriteOut(DataBuffer const& data);

  Uid destination() const;

 private:
  void ConnectReceive();
  void ConnectSend();

  std::unique_ptr<ClientConnectionManager> MakeConnectionManager(
      Ptr<Cloud> const& cloud);
  std::unique_ptr<CloudServerConnections> MakeDestinationCloudConn(
      ClientConnectionManager& connection_manager);
  ActionPtr<WriteAction> OnWrite(AeMessage&& message);

  ActionContext action_context_;
  PtrView<Client> client_;
  Uid destination_;

  // connection manager to destination cloud
  std::unique_ptr<ClientConnectionManager> dest_conn_manager_;
  std::unique_ptr<CloudServerConnections> dest_cloud_conn_;
  BufferWrite<AeMessage> buffer_write_;
  std::unique_ptr<p2p_stream_internal::MessageSendStream> message_send_stream_;
  std::unique_ptr<p2p_stream_internal::ReadMessageGate> read_message_gate_;

  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  Subscription get_client_cloud_sub_;
};

}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_P2P_MESSAGE_STREAM_H_
