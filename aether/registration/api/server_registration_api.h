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

#  include "aether/types/uid.h"
#  include "aether/crypto/key.h"
#  include "aether/types/server_id.h"
#  include "aether/crypto/crypto_definitions.h"

#  include "aether/reflect/reflect.h"
#  include "aether/crypto/signed_key.h"
#  include "aether/types/data_buffer.h"
#  include "aether/crypto/icrypto_provider.h"
#  include "aether/api_protocol/api_protocol.h"
#  include "aether/work_cloud_api/server_descriptor.h"

#  include "aether/registration/api/global_reg_server_api.h"

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

class ServerRegistrationApi : public ApiClass {
  class RegistrationProc {
   public:
    explicit RegistrationProc(ServerRegistrationApi& api) : api_{&api} {}

    template <typename... Args>
    auto operator()(std::string&& salt, std::string&& password_suffix,
                    std::vector<uint32_t>&& passwords, Uid parent_uid_,
                    SubApi<GlobalRegServerApi> const& sub_api) {
      auto def_proc = DefaultArgProc{};
      return def_proc(std::move(salt), std::move(password_suffix),
                      std::move(passwords), std::move(parent_uid_),
                      api_->Encrypt(sub_api(api_->global_reg_server_api_)));
    }

   private:
    ServerRegistrationApi* api_;
  };

 public:
  ServerRegistrationApi(ProtocolContext& protocol_context,
                        ActionContext action_context,
                        IEncryptProvider& encrypt_provider);

  Method<3,
         void(std::string salt, std::string password_suffix,
              std::vector<uint32_t> passwords, Uid parent_uid_,
              SubApi<GlobalRegServerApi> sub_api),
         RegistrationProc>
      registration;

  Method<4, ApiPromisePtr<PowParams>(Uid parent_id, PowMethod pow_method)>
      request_proof_of_work_data;

  Method<5, ApiPromisePtr<std::vector<ServerDescriptor>>(
                std::vector<ServerId> servers)>
      resolve_servers;

  Method<6, void(Key key)> set_return_key;

 private:
  DataBuffer Encrypt(DataBuffer const& data) const;

  IEncryptProvider* encrypt_provider_;
  GlobalRegServerApi global_reg_server_api_;
};
}  // namespace ae

#endif
#endif  // AETHER_REGISTRATION_API_SERVER_REGISTRATION_API_H_
