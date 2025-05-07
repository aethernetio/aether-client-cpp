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

#ifndef AETHER_CLIENT_CONNECTIONS_CLIENT_CONNECTION_H_
#define AETHER_CLIENT_CONNECTIONS_CLIENT_CONNECTION_H_

#include "aether/uid.h"
#include "aether/reflect/reflect.h"
#include "aether/stream_api/istream.h"

namespace ae {
/**
 * \brief Client's connection for messaging interface
 */
class ClientConnection {
 public:
  using NewStreamEvent =
      Event<void(Uid uid, std::unique_ptr<ByteIStream> stream)>;

  virtual ~ClientConnection() = default;

  /**
   * \brief Create a stream to another client to send messages.
   */
  virtual std::unique_ptr<ByteIStream> CreateStream(Uid destination_uid) = 0;

  /**
   * \brief Event when a new stream is opened by another client.
   */
  virtual NewStreamEvent::Subscriber new_stream_event() = 0;

  /**
   * \brief Close a stream by uid.
   */
  virtual void CloseStream(Uid uid) = 0;

  AE_REFLECT()
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_CLIENT_CONNECTION_H_
