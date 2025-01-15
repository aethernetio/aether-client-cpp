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

#include "aether/methods/client_api/client_safe_api.h"

#include <cassert>
#include <utility>

#include "aether/api_protocol/api_protocol.h"

namespace ae {

void ClientSafeApi::LoadFactory(MessageId message_id, ApiParser& parser) {
  switch (message_id) {
    case StreamToClient::kMessageCode:
      parser.Load<StreamToClient>(*this);
      break;
    case SendMessage::kMessageCode:
      parser.Load<SendMessage>(*this);
      break;
    default:
      if (!ExtendsApi::LoadExtend(message_id, parser)) {
        assert(false);
      }
  }
}

void ClientSafeApi::Execute(StreamToClient&& message, ApiParser& api_parser) {
  api_parser.Context().MessageNotify(std::move(message));
}

void ClientSafeApi::Execute(SendMessage&& message, ApiParser& api_parser) {
  api_parser.Context().MessageNotify(std::move(message));
}

}  // namespace ae
