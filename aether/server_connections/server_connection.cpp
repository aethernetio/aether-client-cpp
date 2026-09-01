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

#include <algorithm>
#include <cassert>

#include "aether/aether.h"
#include "aether/channels/channel.h"
#include "aether/config.h"
#include "aether/server.h"
#include "aether/types/address.h"

#include "aether/tele.h"
#include "aether/ae_exp_diag.h"
#include "aether-miscpp/format/format.h"

namespace ae {

ServerConnection::ServerConnection(AeContext const& ae_context,
                                   Ptr<Server> const& server)
    : ae_context_{ae_context},
      server_{server},
      full_connected_{false},
      top_channel_{nullptr} {
  InitChannels();
  SelectChannel();
}

WriteAction& ServerConnection::Write(DataBuffer&& in_data) {
  // Write allowed only if stream_info.is_writable == true
  assert(stream_info_.is_writable && "Channel is not writable");
  assert((top_channel_ != nullptr) && "channel connection is not available");

  assert(!!stream_ && "channel stream is not available");

  return stream_->Write(std::move(in_data));
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
  // restream means something wrong with current channel
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
  // ensure server is alive
  [[maybe_unused]] auto server = server_.Lock();
  assert(server && "Server is null");

  // ensure channel is alive
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
    auto channel = c.Load();
    // Skip channels whose transport was compiled out (e.g. UDP-only state
    // loaded into a TCP-only build). Factory returns nullptr for those and
    // the connection would never link.
    if (auto endpoint = channel->endpoint()) {
#if !AE_SUPPORT_TCP
      if (endpoint->protocol == Protocol::kTcp) {
        AE_TELED_DEBUG("Skip unsupported TCP channel {}", *endpoint);
        continue;
      }
#endif
#if !AE_SUPPORT_UDP
      if (endpoint->protocol == Protocol::kUdp) {
        AE_TELED_DEBUG("Skip unsupported UDP channel {}", *endpoint);
        continue;
      }
#endif
    }
    channels.emplace_back(std::move(channel));
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
    channels_.emplace_back(c);
  }
}

ChannelEntry* ServerConnection::TopChannel() {
  auto it = std::find_if(std::begin(channels_), std::end(channels_),
                         [](auto const& entry) { return !entry.failed; });
  if (it == std::end(channels_)) {
    AE_EXP_LC("ServerConnection", 0, this, 0, "TopChannel", "none");
    return nullptr;
  }
  auto* entry = &*it;
  if (auto channel = entry->channel.Lock()) {
    auto ep = channel->endpoint();
    if (ep) {
      auto const ep_str = Format("{}", *ep);
      AE_EXP_LC("ServerConnection", 0, this, 0, "TopChannel", "endpoint=%s",
                ep_str.c_str());
    } else {
      AE_EXP_LC("ServerConnection", 0, this, 0, "TopChannel", "selected");
    }
  } else {
    AE_EXP_LC("ServerConnection", 0, this, 0, "TopChannel", "selected");
  }
  return entry;
}

void ServerConnection::SelectChannel() {
  // prevent new channel selection while one is active
  if (channel_select_action_ && !channel_select_action_->is_finished()) {
    AE_TELED_DEBUG("Repeated select channel");
    return;
  }

  AE_EXP_LC("ServerConnection", 0, this, 0, "SelectChannel", "start");
  AE_TELED_DEBUG("Select channel");
  auto* top = TopChannel();
  if (top == nullptr) {
    DeferServerError();
    return;
  }

  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_writable = false;
  stream_update_event_.Emit();

  channel_select_action_.emplace(ae_context_, *top);
  channel_select_action_->result_event().Subscribe([this](auto&& res) noexcept {
    if (res) {
      ChannelUpdated(channel_select_action_->attempted_channel(),
                     std::forward<decltype(res)>(res).value());
    } else {
      // Mark the channel that was actually being built. top_channel_ is only
      // set after a successful build and must not be used here.
      ChannelBuildFailed(channel_select_action_->attempted_channel());
    }
  });
  // Subscribe before Start so a synchronous TransportBuilder result is not
  // lost.
  channel_select_action_->Start();

  AE_TELED_DEBUG("New channel selected");
  channel_changed_.Emit();
  stream_update_event_.Emit();
}

void ServerConnection::DeferSelectChannel() {
  // Must not call SelectChannel() from inside ChannelSelectAction::result
  // callback: recreating transport_waiter on that stack is unsafe even though
  // the action is already finished.
  defer_sub_ = ae_context_.scheduler().Task([this]() {
    AE_TELED_DEBUG("SERVER_CHANNEL_RESELECT_SCHEDULED");
    SelectChannel();
  });
  assert(!!defer_sub_);
}

void ServerConnection::ChannelUpdated(ChannelEntry& new_channel,
                                      std::unique_ptr<ByteIStream>&& stream) {
  AE_TELED_DEBUG("Channel updated");
  top_channel_ = &new_channel;
  stream_ = std::move(stream);
  assert(stream_->stream_info().link_state == LinkState::kLinked &&
         "New channel should be linked");

  // track channel stream_ link error
  channel_stream_update_sub_ =
      stream_->stream_update_event().Subscribe([this, s_ = stream_.get()]() {
        auto info = s_->stream_info();
        if (info.link_state == LinkState::kLinkError) {
          ChannelError();
        }
      });

  channel_stream_out_data_sub_ = stream_->out_data_event().Subscribe(
      MethodPtr<&ServerConnection::OnRead>{this});

  auto channel = top_channel_->channel.Lock();
  assert(channel && "Cahnel is null");

  auto channel_props = channel->transport_properties();
  stream_info_.is_reliable =
      (channel_props.reliability == Reliability::kReliable);
  stream_info_.rec_element_size = channel_props.rec_packet_size;
  stream_info_.max_element_size = channel_props.max_packet_size;

  // now it's safe to write to server stream_
  stream_info_.link_state = LinkState::kLinked;
  stream_info_.is_writable = true;
  if (auto ep = channel->endpoint()) {
    auto const ep_str = Format("{}", *ep);
    AE_EXP_LC("ServerConnection", 0, this, 0, "ChannelUpdated",
              "writable=1 endpoint=%s", ep_str.c_str());
  } else {
    AE_EXP_LC("ServerConnection", 0, this, 0, "ChannelUpdated", "writable=1");
  }
  stream_update_event_.Emit();
}

void ServerConnection::ChannelBuildFailed(ChannelEntry& attempted_channel) {
  AE_TELED_ERROR("SERVER_CHANNEL_BUILD_FAILED");
  if (auto channel = attempted_channel.channel.Lock()) {
    if (auto ep = channel->endpoint()) {
      auto const ep_str = Format("{}", *ep);
      AE_EXP_LC("ServerConnection", 0, this, 0, "ChannelBuildFailed",
                "endpoint=%s", ep_str.c_str());
    } else {
      AE_EXP_LC("ServerConnection", 0, this, 0, "ChannelBuildFailed", "");
    }
  } else {
    AE_EXP_LC("ServerConnection", 0, this, 0, "ChannelBuildFailed", "");
  }
  channel_stream_update_sub_.Reset();
  channel_stream_out_data_sub_.Reset();
  stream_info_.is_writable = false;
  attempted_channel.failed = true;

  if (full_connected_) {
    ServerError();
  } else {
    DeferSelectChannel();
  }
}

void ServerConnection::ServerError() {
  AE_TELED_ERROR("SERVER_CONNECTION_ERROR");
  channel_stream_update_sub_.Reset();
  channel_stream_out_data_sub_.Reset();
  // TODO: should we also reset connection.stream()

  stream_info_.link_state = LinkState::kLinkError;
  stream_info_.is_writable = false;

  server_error_.Emit();
  stream_update_event_.Emit();
}

void ServerConnection::ChannelError() {
  AE_TELED_ERROR("Channel error");
  channel_stream_update_sub_.Reset();
  channel_stream_out_data_sub_.Reset();
  stream_info_.is_writable = false;

  if (top_channel_ != nullptr) {
    top_channel_->failed = true;
  }

  // if full_connected_ it wasn't channel error but server error
  if (full_connected_) {
    ServerError();
  } else {
    DeferSelectChannel();
  }
}

void ServerConnection::DeferServerError() {
  stream_info_.is_writable = false;
  defer_sub_ = ae_context_.scheduler().Task([&]() { ServerError(); });
  if (!defer_sub_) {
    AE_TELED_ERROR("DeferServerError schedule failed; invoking ServerError");
    ServerError();
  }
}

void ServerConnection::OnRead(DataBuffer const& data) {
  full_connected_ = true;
  out_data_event_.Emit(data);
}

}  // namespace ae
