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

#include "aether/methods/client_reg_api/client_reg_root_api.h"

namespace ae {
void ClientRegRootApi::LoadFactory(MessageId message_id, ApiParser& parser) {
  switch (message_id) {
    case Enter::kMessageCode:
      parser.Load<Enter>(*this);
      break;
    default:
      if (!ExtendsApi::LoadExtend(message_id, parser)) {
        assert(false);
      }
      break;
  }
}

void ClientRegRootApi::Execute(Enter&& message, ApiParser& parser) {
  parser.Context().MessageNotify(std::move(message));
}

}  // namespace ae
