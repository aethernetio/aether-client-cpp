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
#  include "aether/crypto/crypto_definitions.h"
#  include "aether/crypto/icrypto_provider.h"
#  include "aether/crypto/signed_key.h"

#  include "aether/api_protocol/api_protocol.h"
#  include "aether/registration/api/server_registration_api.h"
#  include "aether/work_cloud_api/info_ip.h"

namespace ae {

class RegistrationRootApi : public DeclareApi<RegistrationRootApi> {
 public:
  RegistrationRootApi(IEncryptProvider& root_encrypt,
                      IEncryptProvider& global_encrypt);

  ApiPromise<SignedKey> GetPublicKey(CryptoLib crypto_lib);
  void Enter(CryptoLib crypto_lib, SubApi<ServerRegistrationApi> sub_api);

  ApiPromise<InfoIp> GetMyIp();

  API_LIST(METHOD(3, GetPublicKey), METHOD(4, Enter), METHOD(6, GetMyIp))

 private:
  IEncryptProvider* enc_provider_;
  ServerRegistrationApi server_registration_api_;
};
}  // namespace ae
#endif
#endif  // AETHER_REGISTRATION_API_REGISTRATION_ROOT_API_H_
