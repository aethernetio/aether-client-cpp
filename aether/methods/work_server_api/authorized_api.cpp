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

#include "aether/methods/work_server_api/authorized_api.h"

#include <utility>

namespace ae {

void AuthorizedApi::Pack(Ping&& message, ApiPacker& api_packer) {
  api_packer.Pack(Ping::kMessageCode, std::move(message));
}

void AuthorizedApi::Pack(SendMessage&& message, ApiPacker& api_packer) {
  api_packer.Pack(SendMessage::kMessageCode, std::move(message));
}

void AuthorizedApi::Pack(Resolvers&& message, ApiPacker& api_packer) {
  api_packer.Pack(Resolvers::kMessageCode, std::move(message));
}

void AuthorizedApi::Pack(CheckAccessForSendMessage&& message,
                         ApiPacker& api_packer) {
  api_packer.Pack(Resolvers::kMessageCode, std::move(message));
}
}  // namespace ae
