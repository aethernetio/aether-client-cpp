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
RegistrationRootApi::RegistrationRootApi(IEncryptProvider& root_encrypt,
                                         IEncryptProvider& global_encrypt)
    : enc_provider_{&root_encrypt}, server_registration_api_{global_encrypt} {}

ApiPromise<SignedKey> RegistrationRootApi::GetPublicKey(CryptoLib crypto_lib) {
  return ClientMethod<&RegistrationRootApi::GetPublicKey>(crypto_lib);
}

void RegistrationRootApi::Enter(CryptoLib crypto_lib,
                                SubApi<ServerRegistrationApi> sub_api) {
  auto server_api_data = enc_provider_->Encrypt(
      std::move(sub_api)(server_registration_api_, client_context()));
  ClientMethod<&RegistrationRootApi::Enter>(crypto_lib,
                                            std::move(server_api_data));
}

ApiPromise<InfoIp> RegistrationRootApi::GetMyIp() {
  return ClientMethod<&RegistrationRootApi::GetMyIp>();
}

}  // namespace ae
#endif
