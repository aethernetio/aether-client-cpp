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

#ifndef AETHER_REGISTRATION_API_REGISTRATION_ROOT_API_H_
#define AETHER_REGISTRATION_API_REGISTRATION_ROOT_API_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION
#  include "aether/crypto/signed_key.h"
#  include "aether/crypto/crypto_definitions.h"

#  include "aether/types/data_buffer.h"
#  include "aether/crypto/icrypto_provider.h"
#  include "aether/api_protocol/api_protocol.h"

#  include "aether/registration/api/server_registration_api.h"

namespace ae {

class RegistrationRootApi : public ApiClass {
  class EnterProc {
   public:
    explicit EnterProc(RegistrationRootApi& api) : api_{&api} {}

    auto operator()(CryptoLib crypto_lib,
                    SubApi<ServerRegistrationApi> const& sub_api) {
      auto def_proc = DefaultArgProc{};
      return def_proc(crypto_lib,
                      api_->Encrypt(sub_api(api_->server_registration_api_)));
    }

   private:
    RegistrationRootApi* api_;
  };

 public:
  RegistrationRootApi(ProtocolContext& protocol_context,
                      ActionContext action_context,
                      IEncryptProvider& root_encrypt,
                      IEncryptProvider& global_encrypt);

  Method<03, ApiPromisePtr<SignedKey>(CryptoLib crypto_lib)>
      get_asymmetric_public_key;
  Method<04, void(CryptoLib crypto_lib, SubApi<ServerRegistrationApi> sub_api),
         EnterProc>
      enter;

 private:
  DataBuffer Encrypt(DataBuffer const& data) const;

  IEncryptProvider* enc_provider_;
  ServerRegistrationApi server_registration_api_;
};
}  // namespace ae
#endif
#endif  // AETHER_REGISTRATION_API_REGISTRATION_ROOT_API_H_
