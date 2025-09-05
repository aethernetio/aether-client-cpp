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

#ifndef AETHER_SERVER_CONNECTIONS_SERVER_CHANNEL_H_
#define AETHER_SERVER_CONNECTIONS_SERVER_CHANNEL_H_

#include <cassert>

#include "aether/common.h"
#include "aether/memory.h"

#include "aether/ptr/ptr_view.h"
#include "aether/actions/timer_action.h"
#include "aether/actions/action_context.h"

#include "aether/stream_api/istream.h"
#include "aether/stream_api/buffer_stream.h"

#include "aether/transport/build_transport_action.h"

namespace ae {
class Channel;
class ServerChannel final {
 public:
  ServerChannel(ActionContext action_context, Channel::ptr const& channel);

  AE_CLASS_NO_COPY_MOVE(ServerChannel)

  ByteIStream& stream();

 private:
  void OnConnected(BuildTransportAction& build_transport_action);
  void OnConnectedFailed();

  ActionContext action_context_;
  PtrView<Channel> channel_;

  std::unique_ptr<ByteIStream> transport_stream_;
  BufferStream buffer_stream_;

  ActionPtr<BuildTransportAction> build_transport_action_;
  Subscription build_transport_sub_;

  TimePoint connection_start_time_;
  ActionPtr<TimerAction> connection_timer_;

  Subscription connection_timeout_;
  Subscription connection_error_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_SERVER_CHANNEL_H_
