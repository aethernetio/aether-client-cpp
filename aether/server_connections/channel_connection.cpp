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

#include "aether/tele/tele.h"

namespace ae {
ChannelConnection::ChannelConnection(AeContext ae_context,
                                     Ptr<Channel> const& channel)
    : ae_context_{ae_context} {
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
  transport_build_start_ = Now();
  auto sender = channel->TransportBuilder();
  transport_waiter_.emplace(
      ae_context_, std::move(sender),
      [&, c{PtrView<Channel>{channel}}](auto&& result) {
        assert(!!result);
        build_timer_active_ = false;
        if (result->IsOk()) {
          auto channel = c.Lock();
          assert(channel && "Channel not loaded");
          auto build_time = std::chrono::duration_cast<Duration>(
              Now() - transport_build_start_);
          AE_TELED_ERROR("Transport built for {:%S}", build_time);
          channel->channel_statistics().AddConnectionTime(build_time);

          transport_stream_ = std::move(result->value());
          assert(transport_stream_ && "Transport should be created");
          connection_state_event_.Emit(true);
        } else {
          AE_TELED_ERROR("Transport build failed");
          connection_state_event_.Emit(false);
        }
        transport_waiter_.reset();
      });

  // setup timeout for transport build
  ae_context_.scheduler().DelayedTask(
      [&]() {
        if (!build_timer_active_) {
          return;
        }
        AE_TELED_ERROR("Transport build timed out");
        connection_state_event_.Emit(false);
      },
      channel->TransportBuildTimeout());
}

}  // namespace ae
