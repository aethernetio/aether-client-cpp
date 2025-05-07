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

#ifndef AETHER_CLIENT_CONNECTIONS_SPLIT_STREAM_CLIENT_CONNECTION_H_
#define AETHER_CLIENT_CONNECTIONS_SPLIT_STREAM_CLIENT_CONNECTION_H_

#include <map>

#include "aether/uid.h"
#include "aether/memory.h"
#include "aether/client.h"
#include "aether/events/events.h"
#include "aether/actions/action_context.h"

#include "aether/stream_api/stream_api.h"
#include "aether/stream_api/stream_splitter.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_connections/client_connection.h"

namespace ae {
class SplitStreamCloudConnection final {
  struct ClientStreams {
    std::unique_ptr<P2pStream> p2p_stream;
    std::unique_ptr<StreamSplitter> stream_splitter;
  };

  class ConnectStream final : public ByteStream {
   public:
    explicit ConnectStream(SplitStreamCloudConnection& self, Uid uid,
                           StreamId id);
    ~ConnectStream() override;

   private:
    SplitStreamCloudConnection* self_;
    Uid uid_;
    StreamId id_;
  };

 public:
  using NewStreamEvent = Event<void(Uid uid, StreamId stream_id,
                                    std::unique_ptr<ByteIStream> stream)>;

  explicit SplitStreamCloudConnection(ActionContext action_context,
                                      Ptr<Client> const& client,
                                      ClientConnection& client_connection);

  AE_CLASS_NO_COPY_MOVE(SplitStreamCloudConnection)

  std::unique_ptr<ByteIStream> CreateStream(Uid uid, StreamId id);
  void CloseStream(Uid uid, StreamId id);

  NewStreamEvent::Subscriber new_stream_event();

 private:
  std::map<Uid, std::unique_ptr<ClientStreams>>::iterator RegisterStream(
      Uid uid);
  void OnNewStream(Uid uid, std::unique_ptr<ByteIStream> stream);

  ClientConnection* client_connection_;
  ActionContext action_context_;
  PtrView<Client> client_;
  Subscription client_new_stream_sub_;
  MultiSubscription new_split_stream_subs_;
  NewStreamEvent new_stream_event_;

  std::map<Uid, std::unique_ptr<ClientStreams>> streams_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_SPLIT_STREAM_CLIENT_CONNECTION_H_
