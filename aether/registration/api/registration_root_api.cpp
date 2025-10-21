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

#include "aether/registration/api/registration_root_api.h"

#if AE_SUPPORT_REGISTRATION
namespace ae {
RegistrationRootApi::RegistrationRootApi(ProtocolContext& protocol_context,
                                         ActionContext action_context,
                                         IEncryptProvider& root_encrypt,
                                         IEncryptProvider& global_encrypt)
    : ApiClass{protocol_context},
      get_asymmetric_public_key{protocol_context, action_context},
      enter{protocol_context, EnterProc{*this}},
      enc_provider_{&root_encrypt},
      server_registration_api_{protocol_context, action_context,
                               global_encrypt} {}

DataBuffer RegistrationRootApi::Encrypt(DataBuffer const& data) const {
  return enc_provider_->Encrypt(data);
}

}  // namespace ae
#endif
