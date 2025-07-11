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

#include "aether/methods/client_reg_api/client_reg_api.h"

#if AE_SUPPORT_REGISTRATION

#  include "aether/api_protocol/api_protocol.h"

namespace ae {
ClientApiRegSafe::ClientApiRegSafe(ProtocolContext& protocol_context)
    : ReturnResultApiImpl{protocol_context} {}

void ClientApiRegSafe::GlobalApi(ApiParser&, DataBuffer data) {
  global_api_event.Emit(data);
}

}  // namespace ae
#endif
