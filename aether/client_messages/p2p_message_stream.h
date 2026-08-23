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

#include "aether/ae_context.h"
#include "aether/ptr/ptr_view.h"
#include "aether/types/uid.h"

#include "aether/write_action/buffer_write.h"

#include "aether/client_messages/p2p_port_handle.h"
#include "aether/cloud_connections/cloud_server_connections.h"
#include "aether/connection_manager/client_cloud_manager.h"
#include "aether/types/address.h"
#include "aether/types/server_id.h"

namespace ae {
class Client;
class Cloud;
namespace p2p_stream_internal {
class MessageSendStream;
}  // namespace p2p_stream_internal

// Diagnostic snapshot of the MainServer destination used by P2pStream::Write.
// Not a public routing API: production Write does not depend on this.
struct P2pSendRouteSnapshot {
  bool present{false};
  ServerId server_id{};
  Protocol protocol{Protocol::kTcp};
  std::uint64_t route_generation{0};
  bool linked{false};
  bool writable{false};
  std::size_t ping_sample_count{0};
  Duration min_rtt{};
  Duration p99_rtt{};
};

class P2pStream final : public ByteIStream {
 public:
  P2pStream(AeContext const& ae_context, Ptr<Client> const& client,
            Uid destination, P2pPortHandle handle);

  ~P2pStream() override;

  AE_CLASS_NO_COPY_MOVE(P2pStream);

  WriteAction& Write(DataBuffer&& data) override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

  void WriteOut(DataBuffer const& data);
  Uid const& destination() const;
  P2pSendRouteSnapshot InspectSendRoute() const;
  P2pSendRouteSnapshot LastSendRoute() const;

 private:
  void ConnectReceive();
  void ConnectSend();

  std::unique_ptr<CloudServerConnections> MakeDestinationCloudConn(
      Ptr<Cloud> const& cloud,
      std::unique_ptr<IServerConnectionFactory> factory);
  WriteAction* OnWrite(AeMessage&& message);

  AeContext ae_context_;
  PtrView<Client> client_;
  Uid destination_{};

  P2pPortHandle handle_;

  // connection to destination cloud
  std::unique_ptr<CloudServerConnections> dest_cloud_conn_;
  BufferWrite<AeMessage, 100> buffer_write_;
  std::unique_ptr<p2p_stream_internal::MessageSendStream> message_send_stream_;

  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  Subscription get_client_cloud_sub_;
  Subscription out_data_sub_;
  P2pSendRouteSnapshot last_send_route_{};
};

}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_P2P_MESSAGE_STREAM_H_
