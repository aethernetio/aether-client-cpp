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

#ifndef AETHER_REGISTRATION_API_SERVER_REGISTRATION_API_H_
#define AETHER_REGISTRATION_API_SERVER_REGISTRATION_API_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION

#  include <string>
#  include <vector>

#  include "aether-miscpp/reflect/reflect.h"

#  include "aether/crypto/crypto_definitions.h"
#  include "aether/crypto/icrypto_provider.h"
#  include "aether/crypto/key.h"
#  include "aether/crypto/signed_key.h"
#  include "aether/types/server_id.h"
#  include "aether/types/uid.h"

#  include "aether/api_protocol/api_protocol.h"
#  include "aether/registration/api/global_reg_server_api.h"
#  include "aether/work_cloud_api/server_descriptor.h"

namespace ae {
struct PowParams {
  AE_REFLECT_MEMBERS(salt, password_suffix, pool_size, max_hash_value,
                     global_key)
  std::string salt;
  std::string password_suffix;
  std::uint8_t pool_size;
  std::uint32_t max_hash_value;
  SignedKey global_key;
};

class ServerRegistrationApi : public DeclareApi<ServerRegistrationApi> {
 public:
  explicit ServerRegistrationApi(IEncryptProvider& encrypt_provider);

  void Registration(std::string const& salt, std::string const& password_suffix,
                    std::vector<std::uint32_t> const& passwords,
                    Uid const& parent_uid_, SubApi<GlobalRegServerApi> sub_api);

  ApiPromise<PowParams> RequestProofOfWorkData(Uid const& parent_id,
                                               PowMethod pow_method);

  ApiPromise<std::vector<ServerDescriptor>> ResolveServers(
      std::vector<ServerId> const& servers);

  void SetReturnKey(Key const& key);

  API_LIST(METHOD(3, Registration), METHOD(4, RequestProofOfWorkData),
           METHOD(5, ResolveServers), METHOD(6, SetReturnKey))

 private:
  IEncryptProvider* encrypt_provider_;
  GlobalRegServerApi global_reg_server_api_;
};
}  // namespace ae

#endif
#endif  // AETHER_REGISTRATION_API_SERVER_REGISTRATION_API_H_
