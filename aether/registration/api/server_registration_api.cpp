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


void ServerRegistrationApi::RegistrationDirect(
    std::string const& salt, std::string const& password_suffix,
    std::vector<std::uint32_t> const& passwords, Uid const& parent_uid,
    Key const& master_key) {
  ClientMethod<&ServerRegistrationApi::RegistrationDirect>(
      salt, password_suffix, passwords, parent_uid, master_key);
}



void ServerRegistrationApi::RequestProofOfWorkData(
    Uid const& parent_id, PowMethod pow_method) {
  ClientMethod<&ServerRegistrationApi::RequestProofOfWorkData>(
      parent_id, pow_method);
}



void ServerRegistrationApi::ResolveServers(
    std::vector<ServerId> const& servers) {
  ClientMethod<&ServerRegistrationApi::ResolveServers>(servers);
}


void ServerRegistrationApi::SetReturnKey(Key const& key) {
  ClientMethod<&ServerRegistrationApi::SetReturnKey>(key);
}

}  // namespace ae
#endif