/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_SERVER_CONNECTIONS_SERVER_CONNECTION_H_
#define AETHER_SERVER_CONNECTIONS_SERVER_CONNECTION_H_

#include <vector>

#include "aether/ae_context.h"
#include "aether/ptr/ptr.h"
#include "aether/ptr/ptr_view.h"
#include "aether/events/events.h"
#include "aether/stream_api/istream.h"
#include "aether/server_connections/channel_connection.h"

namespace ae {
class Server;
class Channel;

class ServerConnection final : public ByteIStream {
  struct ChannelEntry {
    PtrView<Channel> channel;
    bool failed = false;
  };

 public:
  using ServerErrorEvent = Event<void()>;
  using ChannelChangedEvent = Event<void()>;

  ServerConnection(AeContext const& ae_context, Ptr<Server> const& server);

  WriteAction& Write(DataBuffer&& in_data) override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

  ServerErrorEvent::Subscriber server_error_event();
  ChannelChangedEvent::Subscriber channel_changed_event();

  Ptr<Channel> current_channel() const;

 private:
  void InitChannels();
  // return top not failed channel or null if nothing was selected
  ChannelEntry* TopChannel();
  void SelectChannel();
  void ChannelUpdated(ByteIStream& stream);

  void ServerError();
  void ChannelError();
  void DeferServerError();
  void DeferChannelError();

  void OnRead(DataBuffer const& data);

  AeContext ae_context_;
  PtrView<Server> server_;

  bool full_connected_;
  ChannelEntry* top_channel_;
  std::vector<ChannelEntry> channels_;
  std::optional<ChannelConnection> channel_connection_;

  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  ServerErrorEvent server_error_;
  ChannelChangedEvent channel_changed_;
  Subscription channel_stream_update_sub_;
  Subscription channel_stream_out_data_sub_;
  TaskSubscription defer_sub_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_SERVER_CONNECTION_H_
