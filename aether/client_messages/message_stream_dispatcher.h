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

#ifndef AETHER_CLIENT_MESSAGES_MESSAGE_STREAM_DISPATCHER_H_
#define AETHER_CLIENT_MESSAGES_MESSAGE_STREAM_DISPATCHER_H_

#include <map>

#include "aether/uid.h"
#include "aether/ptr/ptr.h"
#include "aether/events/events.h"
#include "aether/events/event_subscription.h"

#include "aether/stream_api/protocol_stream.h"
#include "aether/api_protocol/protocol_context.h"

#include "aether/client_messages/message_stream.h"

#include "aether/methods/client_api/client_safe_api.h"

namespace ae {
class MessageStreamDispatcher {
 public:
  using NewStreamEvent = Event<void(Uid uid, Ptr<MessageStream> stream)>;

  explicit MessageStreamDispatcher(ByteStream& connection_stream);

  NewStreamEvent::Subscriber new_stream_event();

  MessageStream& GetMessageStream(Uid uid);
  void CloseStream(Uid uid);

 private:
  Ptr<MessageStream> CreateMessageStream(Uid uid, StreamId stream_id);
  void OnStreamToClient(
      MessageEventData<ClientSafeApi::StreamToClient> const& msg);

  ProtocolContext protocol_context_;

  ProtocolReadGate<ClientSafeApi> protocol_read_gate_;
  NewStreamEvent new_stream_event_;
  std::map<Uid, Ptr<MessageStream>> streams_;

  Subscription on_client_stream_subscription_;
};

}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_MESSAGE_STREAM_DISPATCHER_H_
