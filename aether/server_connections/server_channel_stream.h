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

#ifndef AETHER_SERVER_CONNECTIONS_SERVER_CHANNEL_STREAM_H_
#define AETHER_SERVER_CONNECTIONS_SERVER_CHANNEL_STREAM_H_

#include <cassert>
#include <optional>

#include "aether/common.h"
#include "aether/memory.h"

#include "aether/ptr/ptr_view.h"
#include "aether/actions/timer_action.h"
#include "aether/actions/action_context.h"

#include "aether/server.h"
#include "aether/channel.h"
#include "aether/adapters/adapter.h"

#include "aether/stream_api/istream.h"
#include "aether/stream_api/buffer_stream.h"
#include "aether/stream_api/transport_write_stream.h"

#include "aether/server_connections/build_transport_action.h"

namespace ae {
class ServerChannelStream final : public ByteIStream {
 public:
  ServerChannelStream(ActionContext action_context, Adapter::ptr const& adapter,
                      Server::ptr const& server, Channel::ptr const& channel);

  AE_CLASS_NO_COPY_MOVE(ServerChannelStream)

  ActionPtr<StreamWriteAction> Write(DataBuffer&& data) override;
  OutDataEvent::Subscriber out_data_event() override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;

 private:
  void OnConnected(BuildTransportAction& build_transport_action);
  void OnConnectedFailed();
  void ConnectTransportToStream();

  ActionContext action_context_;
  PtrView<Server> server_;
  PtrView<Channel> channel_;
  ActionPtr<BuildTransportAction> build_transport_action_;

  BufferStream buffer_stream_;
  std::unique_ptr<ITransport> transport_;
  std::optional<TransportWriteStream> transport_write_gate_;

  TimePoint connection_start_time_;
  ActionPtr<TimerAction> connection_timer_;

  Subscription connection_timeout_;
  Subscription build_transport_sub_;
  Subscription connection_error_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_SERVER_CHANNEL_STREAM_H_
