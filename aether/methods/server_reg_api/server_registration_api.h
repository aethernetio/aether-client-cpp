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

#ifndef AETHER_METHODS_SERVER_REG_API_SERVER_REGISTRATION_API_H_
#define AETHER_METHODS_SERVER_REG_API_SERVER_REGISTRATION_API_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION

#  include <string>
#  include <vector>

#  include "aether/uid.h"
#  include "aether/common.h"
#  include "aether/crypto/key.h"
#  include "aether/crypto/crypto_definitions.h"

#  include "aether/reflect/reflect.h"
#  include "aether/crypto/signed_key.h"
#  include "aether/transport/data_buffer.h"
#  include "aether/api_protocol/api_method.h"
#  include "aether/methods/server_descriptor.h"

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

class ServerRegistrationApi {
 public:
  ServerRegistrationApi(ProtocolContext& protocol_context,
                        ActionContext action_context);

  Method<30, void(std::string salt, std::string password_suffix,
                  std::vector<uint32_t> passwords, Uid parent_uid_,
                  Key return_key, DataBuffer data)>
      registration;

  Method<40, PromiseView<PowParams>(Uid parent_id, PowMethod pow_method,
                                    Key return_key)>
      request_proof_of_work_data;

  Method<70, PromiseView<std::vector<ServerDescriptor>>(
                 std::vector<ServerId> servers)>
      resolve_servers;
};
}  // namespace ae

#endif
#endif  // AETHER_METHODS_SERVER_REG_API_SERVER_REGISTRATION_API_H_
