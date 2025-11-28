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

#ifndef AETHER_REGISTRATION_API_CLIENT_REG_API_SAFE_H_
#define AETHER_REGISTRATION_API_CLIENT_REG_API_SAFE_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION

#  include "aether/api_protocol/api_protocol.h"

namespace ae {

class ClientRegApiSafe final : public ApiClassImpl<ClientRegApiSafe> {
 public:
  explicit ClientRegApiSafe(ProtocolContext& protocol_context);

  ReturnResultApi return_result;
  AE_METHODS(ExtApi<&ClientRegApiSafe::return_result>);
};
}  // namespace ae

#endif
#endif  // AETHER_REGISTRATION_API_CLIENT_REG_API_SAFE_H_
