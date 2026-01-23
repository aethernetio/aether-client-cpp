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

#include "aether/server_connections/server_connection.h"

#include <cassert>
#include <algorithm>

#include "aether/server.h"
#include "aether/channels/channel.h"

namespace ae {
static constexpr std::size_t kBufferCapacity = 200;

ServerConnection::ServerConnection(ActionContext action_context,
                                   Ptr<Server> const& server)
    : action_context_{action_context},
      server_{server},
      buffer_write_{action_context_,
                    MethodPtr<&ServerConnection::OnWrite>{this},
                    kBufferCapacity},
      full_connected_{false} {
  InitChannels();
  SelectChannel();
}

ActionPtr<WriteAction> ServerConnection::Write(DataBuffer&& in_data) {
  return buffer_write_.Write(std::move(in_data));
}

ServerConnection::StreamUpdateEvent::Subscriber
ServerConnection::stream_update_event() {
  return EventSubscriber{stream_update_event_};
}

StreamInfo ServerConnection::stream_info() const { return stream_info_; }

ServerConnection::OutDataEvent::Subscriber ServerConnection::out_data_event() {
  return EventSubscriber{out_data_event_};
}

void ServerConnection::Restream() {
  // restream means current channel has error
  ChannelError();
}

ServerConnection::ServerErrorEvent::Subscriber
ServerConnection::server_error_event() {
  return EventSubscriber{server_error_};
}

ServerConnection::ChannelChangedEvent::Subscriber
ServerConnection::channel_changed_event() {
  return EventSubscriber{channel_changed_};
}

Ptr<Channel> ServerConnection::current_channel() const {
  if (top_channel_ == nullptr) {
    return {};
  }
  auto server = server_.Lock();
  assert(server && "Server is null");

  // ensure channel is loaded
  return top_channel_->channel.Lock();
}

void ServerConnection::InitChannels() {
  auto server = server_.Lock();
  assert(server && "Server is null");

  std::vector<Ptr<Channel>> channels;
  channels.reserve(server->channels.size());
  for (auto c : server->channels) {
    // channel must be loaded before use
    assert(c.is_valid() && "Channel is not loaded");
    channels.emplace_back(c.Load());
  }
  // sort channels by the fastest
  std::sort(std::begin(channels), std::end(channels),
            [&](Ptr<Channel> const& left, Ptr<Channel> const& right) {
              auto l_conn_type = left->transport_properties().connection_type;
              auto r_conn_type = right->transport_properties().connection_type;
              // select the fastest connection type
              if (l_conn_type != r_conn_type) {
                return l_conn_type > r_conn_type;
              }
              // select the lower connection time
              auto l_build_time = left->TransportBuildTimeout();
              auto r_build_time = right->TransportBuildTimeout();
              if (l_build_time != r_build_time) {
                return l_build_time < r_build_time;
              }
              // select the lower ping time
              return left->ResponseTimeout() < right->ResponseTimeout();
            });

  channels_.reserve(channels.size());
  for (auto const& c : channels) {
    channels_.emplace_back(ChannelEntry{c, false});
  }
}

ServerConnection::ChannelEntry* ServerConnection::TopChannel() {
  auto it =
      std::find_if(std::begin(channels_), std::end(channels_),
                   [](ChannelEntry const& entry) { return !entry.failed; });
  if (it == std::end(channels_)) {
    return nullptr;
  }
  return &(*it);
}

void ServerConnection::SelectChannel() {
  top_channel_ = TopChannel();
  if (top_channel_ == nullptr) {
    ServerError();
    return;
  }

  auto server = server_.Lock();
  assert(server && "Server is null");
  auto channel = top_channel_->channel.Lock();
  if (!channel) {
    DeferChannelError();
    return;
  }

  auto channel_props = channel->transport_properties();
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable =
      channel_props.reliability == Reliability::kReliable;
  stream_info_.rec_element_size = channel_props.rec_packet_size;
  stream_info_.max_element_size = channel_props.max_packet_size;
  stream_info_.is_writable = true;

  channel_connection_.emplace(action_context_, channel);
  channel_connection_->connection_state_event().Subscribe(
      [this](bool connected) {
        if (connected) {
          ChannelUpdated();
        } else {
          ChannelError();
        }
      });

  AE_TELED_DEBUG("New channel selected");
  channel_changed_.Emit();
}

void ServerConnection::ChannelUpdated() {
  AE_TELED_DEBUG("Channel updated");
  assert(channel_connection_ && "No channel connection was created");

  auto test_channel = [this]() {
    auto* stream = channel_connection_->stream();
    assert(stream != nullptr && "Channel is not connected");
    auto info = stream->stream_info();
    if (info.link_state == LinkState::kLinkError) {
      ChannelError();
      return false;
    }
    stream_update_event_.Emit();
    return true;
  };

  auto* stream = channel_connection_->stream();
  assert(stream != nullptr && "Channel is not connected");
  channel_stream_update_sub_ =
      stream->stream_update_event().Subscribe(test_channel);

  channel_stream_outd_data_sub_ = stream->out_data_event().Subscribe(
      MethodPtr<&ServerConnection::OnRead>{this});

  stream_info_.link_state = LinkState::kLinked;
  buffer_write_.buffer_off();
  // channel_changed_.Emit();
  stream_update_event_.Emit();
}

void ServerConnection::ServerError() {
  buffer_write_.buffer_on();
  buffer_write_.Drop();
  channel_stream_update_sub_.Reset();
  channel_stream_outd_data_sub_.Reset();
  channel_connection_.reset();
  stream_info_.link_state = LinkState::kLinkError;
  server_error_.Emit();
}

void ServerConnection::DeferChannelError() {
  // Action is used to break SelectChannel recursion
  if (!channel_error_action_ || channel_error_action_->IsFinished()) {
    channel_error_action_ = OwnActionPtr<ChannelErrorAction>{action_context_};
    channel_error_action_->StatusEvent().Subscribe(
        OnResult{[this]() { ChannelError(); }});
  }
  channel_error_action_->Notify();
}

void ServerConnection::ChannelError() {
  AE_TELED_ERROR("Channel error");
  buffer_write_.buffer_on();
  channel_stream_update_sub_.Reset();
  channel_stream_outd_data_sub_.Reset();
  channel_connection_.reset();
  top_channel_->failed = true;

  // if full_connected_ it wasn't channel error but server error
  if (full_connected_) {
    ServerError();
  } else {
    SelectChannel();
  }
}

void ServerConnection::OnRead(DataBuffer const& data) {
  full_connected_ = true;
  out_data_event_.Emit(data);
}

ActionPtr<WriteAction> ServerConnection::OnWrite(DataBuffer&& in_data) {
  if (!channel_connection_) {
    return {};
  }
  auto* stream = channel_connection_->stream();
  if (stream == nullptr) {
    return {};
  }
  return stream->Write(std::move(in_data));
}

}  // namespace ae
