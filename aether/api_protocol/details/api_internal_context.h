/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_API_INTERNAL_DETAILS_CONTEXT_H_
#define AETHER_API_INTERNAL_DETAILS_CONTEXT_H_

#include "aether/api_protocol/details/request_id.h"

namespace ae {
class IPacketList;
class PacketReader;
class ProtocolContext;

class ClientApiContext {
 public:
  virtual IPacketList& packet_list() = 0;
  virtual ProtocolContext& protocol_context() = 0;

 protected:
  ~ClientApiContext() = default;
};

class ServerApiContext {
 public:
  virtual PacketReader& reader() = 0;
  virtual ProtocolContext& protocol_context() = 0;

  /// Request Id installed in context of every method with return value
  void SetRequestId(RequestId id) { id_ = id; }
  RequestId request_id() const { return id_; }

 protected:
  ~ServerApiContext() = default;
  RequestId id_{};
};
}  // namespace ae

#endif  // AETHER_API_INTERNAL_DETAILS_CONTEXT_H_
