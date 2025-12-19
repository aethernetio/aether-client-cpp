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

#include "aether/server_connections/channel_selection_stream.h"

#include <algorithm>

#include "aether/channels/channel.h"

#include "aether/tele/tele.h"

namespace ae {
static constexpr std::size_t kBufferCapacity = 200;

ChannelSelectStream::ChannelSelectStream(ActionContext action_context,
                                         ChannelManager& channel_manager)
    : action_context_{action_context},
      channel_manager_{&channel_manager},
      selected_channel_{},
      buffer_stream_{action_context_, kBufferCapacity},
      stream_info_{},
      is_full_open_{} {
  // TODO: add handle for channels update
  SelectChannel();

  out_data_sub_ = buffer_stream_.out_data_event().Subscribe(
      MethodPtr<&ChannelSelectStream::OutData>{this});
}

ActionPtr<StreamWriteAction> ChannelSelectStream::Write(DataBuffer&& data) {
  if (!server_channel_) {
    AE_TELED_ERROR("There is no channels, write failed");
    return ActionPtr<FailedStreamWriteAction>{action_context_};
  }

  AE_TELED_DEBUG("Write to channel size {}", data.size());
  return buffer_stream_.Write(std::move(data));
}

StreamInfo ChannelSelectStream::stream_info() const { return stream_info_; }

ChannelSelectStream::StreamUpdateEvent::Subscriber
ChannelSelectStream::stream_update_event() {
  return stream_update_event_;
}

ChannelSelectStream::OutDataEvent::Subscriber
ChannelSelectStream::out_data_event() {
  return out_data_event_;
}

ServerChannel const* ChannelSelectStream::server_channel() const {
  return server_channel_.get();
}

void ChannelSelectStream::Restream() {
  AE_TELED_DEBUG("Restream channels");
  stream_update_sub_.Reset();

  if (is_full_open_) {
    ServerError();
  } else {
    SelectChannel();
  }
}

std::pair<std::unique_ptr<ServerChannel>, ChannelConnection*>
ChannelSelectStream::TopChannel() {
  auto& channels = channel_manager_->channels();

  std::vector<ChannelConnection*> filtered_channels;
  for (auto& ch : channels) {
    if (ch.connection_penalty == 0) {
      filtered_channels.push_back(&ch);
    }
  }

  if (filtered_channels.empty()) {
    return {};
  }
  // TODO: add configuration for channel priorities

  // Select the fastest channel
  auto max_speed_channel = std::min_element(
      filtered_channels.begin(), filtered_channels.end(),
      [](ChannelConnection const* a, ChannelConnection const* b) {
        // the most connection penalty the least preferred channel
        if (a->connection_penalty > b->connection_penalty) {
          return false;
        }

        auto props_a = a->channel()->transport_properties();
        auto props_b = b->channel()->transport_properties();
        // select the fastest connection type
        if (props_a.connection_type > props_b.connection_type) {
          return true;
        }
        if (props_a.connection_type == props_b.connection_type) {
          // select the lower connection time
          if (a->channel()->TransportBuildTimeout() <
              b->channel()->TransportBuildTimeout()) {
            return true;
          }
          // select the lower ping time
          if (a->channel()->ResponseTimeout() <
              b->channel()->ResponseTimeout()) {
            return true;
          }
        }
        return false;
      });

  return std::pair{(*max_speed_channel)->GetServerChannel(),
                   *max_speed_channel};
}

void ChannelSelectStream::SelectChannel() {
  stream_update_sub_.Reset();
  buffer_stream_.Unlink();
  is_full_open_ = false;

  // add penalty for previous channel
  if (selected_channel_ != nullptr) {
    selected_channel_->connection_penalty++;
  }

  std::tie(server_channel_, selected_channel_) = TopChannel();
  if (!server_channel_) {
    AE_TELED_ERROR("Unable to select channel, server error");
    stream_info_.link_state = LinkState::kLinkError;
    stream_update_event_.Emit();
    return;
  }

  AE_TELED_DEBUG("New channel selected");

  auto const& tp = server_channel_->channel()->transport_properties();
  stream_info_.max_element_size = tp.max_packet_size;
  stream_info_.rec_element_size = tp.rec_packet_size;
  stream_info_.is_reliable = (tp.reliability == Reliability::kReliable);

  stream_info_.link_state = LinkState::kUnlinked;

  channel_connection_sub_ =
      server_channel_->connection_result().Subscribe([this](auto is_connected) {
        if (is_connected) {
          LinkStream();
        } else {
          SelectChannel();
        }
      });
}

void ChannelSelectStream::LinkStream() {
  assert(server_channel_);
  auto* stream = server_channel_->stream();
  assert(stream != nullptr);

  Tie(buffer_stream_, *stream);

  stream_update_sub_ = stream->stream_update_event().Subscribe(
      MethodPtr<&ChannelSelectStream::StreamUpdate>{this});

  StreamUpdate();
}

void ChannelSelectStream::StreamUpdate() {
  assert(server_channel_ && "Call stream update on empty server channel");
  auto* stream = server_channel_->stream();
  assert(stream != nullptr && "Stream is null");

  auto info = stream->stream_info();
  if (info.link_state == LinkState::kLinkError) {
    AE_TELED_DEBUG("Transport link error");
    if (is_full_open_) {
      ServerError();
    } else {
      AE_TELED_DEBUG("Select channel");
      SelectChannel();
    }
    return;
  }

  stream_info_.is_writable = info.is_writable;
  stream_info_.link_state = info.link_state;
  stream_update_event_.Emit();
}

void ChannelSelectStream::ServerError() {
  AE_TELED_DEBUG("Server error");
  stream_update_sub_.Reset();
  buffer_stream_.Unlink();
  server_channel_.reset();
  stream_info_.link_state = LinkState::kLinkError;
  stream_update_event_.Emit();
}

void ChannelSelectStream::OutData(DataBuffer const& data) {
  // Received data through selected channel
  // Connection to the server is fully opened
  is_full_open_ = true;

  out_data_event_.Emit(data);
}
}  // namespace ae
