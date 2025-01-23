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

#ifndef AETHER_TRANSPORT_SERVER_SERVER_CHANNEL_STREAM_H_
#define AETHER_TRANSPORT_SERVER_SERVER_CHANNEL_STREAM_H_

#include <cassert>
#include <optional>

#include "aether/common.h"

#include "aether/obj/ptr.h"
#include "aether/obj/obj_ptr.h"
#include "aether/actions/action_context.h"
#include "aether/events/multi_subscription.h"

#include "aether/server.h"
#include "aether/channel.h"
#include "aether/adapters/adapter.h"

#include "aether/stream_api/istream.h"
#include "aether/stream_api/buffer_gate.h"
#include "aether/stream_api/transport_write_gate.h"

namespace ae {
class Aether;
class ChannelConnectionAction;

class ServerChannelStream final : public ByteStream {
 public:
  ServerChannelStream(ObjPtr<Aether> const& aether, Adapter::ptr const& adapter,
                      Server::ptr server, Channel::ptr channel);

  AE_CLASS_NO_COPY_MOVE(ServerChannelStream)

  InGate& in() override { return buffer_gate_; }

  void LinkOut(OutGate& /* out */) override { assert(false); }

 private:
  void OnConnected(ChannelConnectionAction const& connection);
  void OnConnectedFailed();

  ActionContext action_context_;
  Server::ptr server_;
  Channel::ptr channel_;

  BufferGate buffer_gate_;
  std::optional<TransportWriteGate> transport_write_gate_;

  Ptr<class ChannelConnectionAction> connection_action_;
  MultiSubscription connection_subscriptions_;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_SERVER_SERVER_CHANNEL_STREAM_H_
