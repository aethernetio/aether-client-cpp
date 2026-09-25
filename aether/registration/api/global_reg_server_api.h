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

#ifndef AETHER_REGISTRATION_API_GLOBAL_REG_SERVER_API_H_
#define AETHER_REGISTRATION_API_GLOBAL_REG_SERVER_API_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION
#  include <vector>

#  include "aether-miscpp/reflect/reflect.h"
#  include "aether/api_protocol/api_protocol.h"
#  include "aether/crypto/key.h"
#  include "aether/types/server_id.h"
#  include "aether/types/uid.h"

namespace ae {

struct RegistrationResponse {
  AE_REFLECT_MEMBERS(ephemeral_uid, uid, cloud)
  Uid ephemeral_uid;
  Uid uid;
  std::vector<ServerId> cloud;
};

class GlobalRegServerApi : public DeclareApi<GlobalRegServerApi> {
 public:
  GlobalRegServerApi() = default;

  void SetMasterKey(Key const& key);
  ApiPromise<RegistrationResponse> Finish();

  API_LIST(METHOD(3, SetMasterKey), METHOD(4, Finish));
};

}  // namespace ae
#endif
#endif  // AETHER_REGISTRATION_API_GLOBAL_REG_SERVER_API_H_ */
