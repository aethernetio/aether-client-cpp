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

#ifndef AETHER_SERVER_CONNECTIONS_ISERVER_API_STREAM_H_
#define AETHER_SERVER_CONNECTIONS_ISERVER_API_STREAM_H_

#include "aether/stream_api/istream.h"

#include "aether/api_protocol/api_context.h"
#include "aether/methods/work_server_api/authorized_api.h"

namespace ae {
class IServerApiStream : public ByteIStream {
 public:
  ~IServerApiStream() override = default;

  virtual ApiContext<AuthorizedApi> AuthorizedApi() = 0;
  virtual ProtocolContext& protocol_context() = 0;
  virtual void ServerError() = 0;
};

class IServerApiStreamProvider {
 public:
  virtual ~IServerApiStreamProvider() = default;

  virtual IServerApiStream* ServerApiStream() = 0;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_ISERVER_API_STREAM_H_
