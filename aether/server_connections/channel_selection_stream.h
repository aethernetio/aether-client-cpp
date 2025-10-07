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

#ifndef AETHER_SERVER_CONNECTIONS_CHANNEL_SELECTION_STREAM_H_
#define AETHER_SERVER_CONNECTIONS_CHANNEL_SELECTION_STREAM_H_

#include "aether/stream_api/istream.h"

#include "aether/server_connections/channel_manager.h"

namespace ae {
class ChannelSelectStream final : public ByteIStream {
 public:
  explicit ChannelSelectStream(ActionContext action_context,
                               ChannelManager& channel_manager);

  ActionPtr<StreamWriteAction> Write(DataBuffer&& data) override;
  StreamInfo stream_info() const override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

  ServerChannel const* server_channel() const;

 private:
  std::pair<std::unique_ptr<ServerChannel>, ChannelConnection*> TopChannel();
  void SelectChannel();
  void LinkStream();
  void StreamUpdate();
  void OutData(DataBuffer const& data);

  ActionContext action_context_;
  ChannelManager* channel_manager_;
  ChannelConnection* selected_channel_;
  std::unique_ptr<ServerChannel> server_channel_;
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  bool is_full_open_;

  Subscription channel_connection_sub_;
  Subscription stream_update_sub_;
  Subscription out_data_sub_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_CHANNEL_SELECTION_STREAM_H_
