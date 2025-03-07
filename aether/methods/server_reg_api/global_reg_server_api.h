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

#ifndef AETHER_METHODS_SERVER_REG_API_GLOBAL_REG_SERVER_API_H_
#define AETHER_METHODS_SERVER_REG_API_GLOBAL_REG_SERVER_API_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION
#  include "aether/api_protocol/send_result.h"
#  include "aether/crc.h"
#  include "aether/crypto/key.h"
#  include "aether/api_protocol/api_protocol.h"

namespace ae {

class GlobalRegServerApi : public ApiClass {
 public:
  static constexpr auto kClassId =
      crc32::checksum_from_literal("GlobalRegServerApi");

  struct SetMasterKey : public Message<SetMasterKey> {
    AE_REFLECT_MEMBERS(key)

    Key key;
  };

  struct Finish : public Message<Finish> {
    AE_REFLECT_MEMBERS(request_id)

    RequestId request_id;
  };

  void Pack(SetMasterKey&& message, ApiPacker& packer);
  void Pack(Finish&& message, ApiPacker& packer);
};

}  // namespace ae
#endif
#endif  // AETHER_METHODS_SERVER_REG_API_GLOBAL_REG_SERVER_API_H_ */
