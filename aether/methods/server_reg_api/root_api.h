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
#  include "aether/crypto/crypto_definitions.h"

#  include "aether/stream_api/stream_api.h"
#  include "aether/api_protocol/child_data.h"
#  include "aether/api_protocol/api_protocol.h"

namespace ae {

class RootApi : public ApiClass {
 public:
  struct GetAsymmetricPublicKey : public Message<GetAsymmetricPublicKey> {
    static constexpr MessageId kMessageCode = 3;

    AE_REFLECT_MEMBERS(request_id, crypto_lib_profile)

    RequestId request_id;
    CryptoLibProfile crypto_lib_profile;
  };

  // start a new stream from ClientSafeApi to ServerRegistrationApi with
  // CryptoStream
  struct Enter : public Message<Enter> {
    static constexpr MessageId kMessageCode = 4;

    AE_REFLECT_MEMBERS(stream_id, crypto_lib_profile)
    StreamId stream_id;
    CryptoLibProfile crypto_lib_profile;
  };

  void Pack(GetAsymmetricPublicKey message, ApiPacker& packer);
  void Pack(Enter message, ApiPacker& packer);
};
}  // namespace ae
#endif
#endif  // AETHER_METHODS_SERVER_REG_API_ROOT_API_H_ */
