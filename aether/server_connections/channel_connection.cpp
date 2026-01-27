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

#include "aether/server_connections/channel_connection.h"

#include "aether/channels/channel.h"

#include "aether/tele/tele.h"

namespace ae {
ChannelConnection::ChannelConnection(ActionContext action_context,
                                     Ptr<Channel> const& channel)
    : action_context_{action_context} {
  BuildTransport(channel);
}

ByteIStream* ChannelConnection::stream() const {
  return transport_stream_.get();
}

ChannelConnection::ConnectionStateEvent::Subscriber
ChannelConnection::connection_state_event() {
  return EventSubscriber{connection_state_event_};
}

void ChannelConnection::BuildTransport(Ptr<Channel> const& channel) {
  auto builder_action = channel->TransportBuilder();
  transport_build_sub_ = builder_action->StatusEvent().Subscribe(ActionHandler{
      OnResult{[this](auto& builder) {
        transport_stream_ = builder.transport_stream();
        build_timer_->Stop();
        connection_state_event_.Emit(true);
      }},
      OnError{[this]() {
        AE_TELED_ERROR("Transport build failed");
        build_timer_->Stop();
        connection_state_event_.Emit(false);
      }},
  });

  build_timer_ = OwnActionPtr<TimerAction>{action_context_,
                                           channel->TransportBuildTimeout()};
  build_timer_->StatusEvent().Subscribe(OnResult{[this]() {
    AE_TELED_ERROR("Transport build timed out");
    connection_state_event_.Emit(false);
  }});
}

}  // namespace ae
