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

#ifndef AETHER_SERVER_CONNECTIONS_CHANNEL_CONNECTION_H_
#define AETHER_SERVER_CONNECTIONS_CHANNEL_CONNECTION_H_

#include "aether/common.h"
#include "aether/ptr/ptr.h"
#include "aether/ae_context.h"
#include "aether/events/events.h"
#include "aether/channels/channel.h"
#include "aether/stream_api/istream.h"
#include "aether/executors/executors.h"

namespace ae {
class Channel;
class ChannelConnection {
 public:
  using ConnectionStateEvent = Event<void(bool connected)>;

  ChannelConnection(AeContext ae_context, Ptr<Channel> const& channel);

  AE_CLASS_NO_COPY_MOVE(ChannelConnection)

  ByteIStream* stream() const;
  ConnectionStateEvent::Subscriber connection_state_event();

 private:
  void BuildTransport(Ptr<Channel> const& channel);

  AeContext ae_context_;
  std::optional<ex::AsyncWaiter<
      TransportBuildSender,
      SmallFunction<void(
          std::optional<Result<std::unique_ptr<ByteIStream>, int>>)>>>
      transport_waiter_;
  TimePoint transport_build_start_;
  std::unique_ptr<ByteIStream> transport_stream_;
  bool build_timer_active_{};
  ConnectionStateEvent connection_state_event_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_CHANNEL_CONNECTION_H_
