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

#ifndef AETHER_METHODS_CLIENT_API_CLIENT_SAFE_API_H_
#define AETHER_METHODS_CLIENT_API_CLIENT_SAFE_API_H_

#include "aether/uid.h"
#include "aether/transport/data_buffer.h"

#include "aether/events/events.h"
#include "aether/stream_api/stream_api.h"
#include "aether/api_protocol/api_class_impl.h"
#include "aether/api_protocol/return_result_api.h"

namespace ae {

class ClientSafeApi
    : public ReturnResultApiImpl,
      public StreamApiImpl,
      public ApiClassImpl<ClientSafeApi, ReturnResultApiImpl, StreamApiImpl> {
 public:
  explicit ClientSafeApi(ProtocolContext& protocol_context);

  void SendMessage(ApiParser& parser, Uid uid, DataBuffer data);

  using ApiMethods = ImplList<RegMethod<10, &ClientSafeApi::SendMessage>>;

  Event<void(Uid const& uid, DataBuffer const& data)> send_message_event;
};
}  // namespace ae

#endif  // AETHER_METHODS_CLIENT_API_CLIENT_SAFE_API_H_
