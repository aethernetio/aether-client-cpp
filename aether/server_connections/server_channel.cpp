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

#include "aether/server_connections/server_channel.h"

#include "aether/channels/channel.h"

#include "aether/tele/tele.h"

namespace ae {
ServerChannel::ServerChannel(ActionContext action_context,
                             Channel::ptr const& channel)
    : action_context_{action_context},
      channel_{channel},
      build_transport_action_{channel->TransportBuilder()},
      build_transport_sub_{build_transport_action_->StatusEvent().Subscribe(
          ActionHandler{OnResult{[this](auto& action) { OnConnected(action); }},
                        OnError{[this]() { OnConnectedFailed(); }}})},
      connection_start_time_{Now()},
      connection_timer_{action_context_, channel->channel_statistics()
                                             .connection_time_statistics()
                                             .percentile<99>()},
      connection_timeout_{connection_timer_->StatusEvent().Subscribe(
          OnResult{[this](auto const& timer) {
            AE_TELED_ERROR("Connection timeout {:%S}", timer.duration());
            OnConnectedFailed();
          }})} {}

ByteIStream* ServerChannel::stream() { return transport_stream_.get(); }

ObjPtr<Channel> ServerChannel::channel() const {
  auto channel_ptr = channel_.Lock();
  assert(channel_ptr);
  return channel_ptr;
}

ServerChannel::ConnectionResult::Subscriber ServerChannel::connection_result() {
  return EventSubscriber{connection_result_event_};
}

void ServerChannel::OnConnected(
    TransportBuilderAction& transport_builder_action) {
  build_transport_sub_.Reset();

  auto channel_ptr = channel_.Lock();
  assert(channel_ptr);

  connection_timer_->Stop();
  auto connection_time =
      std::chrono::duration_cast<Duration>(Now() - connection_start_time_);
  channel_ptr->channel_statistics().AddConnectionTime(connection_time);

  transport_stream_ = transport_builder_action.transport_stream();
  connection_result_event_.Emit(true);
}

void ServerChannel::OnConnectedFailed() {
  AE_TELED_ERROR("ServerChannelStream:OnConnectedFailed");
  connection_timeout_.Reset();
  build_transport_sub_.Reset();
  connection_result_event_.Emit(false);
}

}  // namespace ae
