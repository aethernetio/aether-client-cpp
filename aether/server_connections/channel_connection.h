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
#include "aether/events/events.h"
#include "aether/stream_api/istream.h"
#include "aether/actions/timer_action.h"
#include "aether/actions/action_context.h"
#include "aether/events/event_subscription.h"

namespace ae {
class Channel;
class ChannelConnection {
 public:
  using ConnectionStateEvent = Event<void(bool connected)>;

  ChannelConnection(ActionContext action_context, Ptr<Channel> const& channel);

  AE_CLASS_NO_COPY_MOVE(ChannelConnection)

  ByteIStream* stream() const;
  ConnectionStateEvent::Subscriber connection_state_event();

 private:
  void BuildTransport(Ptr<Channel> const& channel);

  ActionContext action_context_;
  std::unique_ptr<ByteIStream> transport_stream_;
  Subscription transport_build_sub_;
  OwnActionPtr<TimerAction> build_timer_;
  ConnectionStateEvent connection_state_event_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_CHANNEL_CONNECTION_H_
