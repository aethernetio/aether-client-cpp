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

#ifndef AETHER_METHODS_SERVER_REG_API_ROOT_API_H_
#define AETHER_METHODS_SERVER_REG_API_ROOT_API_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION
#  include "aether/crypto/signed_key.h"
#  include "aether/crypto/crypto_definitions.h"

#  include "aether/types/data_buffer.h"
#  include "aether/api_protocol/api_method.h"

namespace ae {

class RootApi {
 public:
  RootApi(ProtocolContext& protocol_context, ActionContext action_context);

  Method<03, PromiseView<SignedKey>(CryptoLibProfile crypto_lib_profile)>
      get_asymmetric_public_key;
  Method<04, void(CryptoLibProfile crypto_lib_profile, DataBuffer data)> enter;
};
}  // namespace ae
#endif
#endif  // AETHER_METHODS_SERVER_REG_API_ROOT_API_H_ */
