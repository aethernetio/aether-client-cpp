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

#include "aether/registration/api/server_registration_api.h"

#if AE_SUPPORT_REGISTRATION

namespace ae {
ServerRegistrationApi::ServerRegistrationApi(IEncryptProvider& encrypt_provider)
    : encrypt_provider_{&encrypt_provider}, global_reg_server_api_{} {}

void ServerRegistrationApi::Registration(
    std::string const& salt, std::string const& password_suffix,
    std::vector<std::uint32_t> const& passwords, Uid const& parent_uid_,
    SubApi<GlobalRegServerApi> sub_api) {
  auto gr_data = encrypt_provider_->Encrypt(
      std::move(sub_api)(global_reg_server_api_, client_context()));
  ClientMethod<&ServerRegistrationApi::Registration>(
      salt, password_suffix, passwords, parent_uid_, std::move(gr_data));
}

ApiPromise<PowParams> ServerRegistrationApi::RequestProofOfWorkData(
    Uid const& parent_id, PowMethod pow_method) {
  return ClientMethod<&ServerRegistrationApi::RequestProofOfWorkData>(
      parent_id, pow_method);
}

ApiPromise<std::vector<ServerDescriptor>> ServerRegistrationApi::ResolveServers(
    std::vector<ServerId> const& servers) {
  return ClientMethod<&ServerRegistrationApi::ResolveServers>(servers);
}

void ServerRegistrationApi::SetReturnKey(Key const& key) {
  ClientMethod<&ServerRegistrationApi::SetReturnKey>(key);
}

}  // namespace ae
#endif
