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

#ifndef AETHER_METHODS_WORK_SERVER_API_LOGIN_API_H_
#define AETHER_METHODS_WORK_SERVER_API_LOGIN_API_H_

#include "aether/uid.h"
#include "aether/crc.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/stream_api/stream_api.h"

namespace ae {
class LoginApi : public ApiClass {
 public:
  static constexpr auto kClassId = crc32::checksum_from_literal("LoginApi");

  // starts a new  stream from ClientApi to AuthorizedApi with CryptoStream
  struct LoginByUid : public Message<LoginByUid> {
    static constexpr auto kMessageCode = 3;

    AE_REFLECT_MEMBERS(stream_id, uid)

    StreamId stream_id;
    Uid uid;
  };

  // starts a new  stream from ClientApi to AuthorizedApi with CryptoStream
  struct LoginByAlias : public Message<LoginByAlias> {
    static constexpr auto kMessageCode = 4;

    AE_REFLECT_MEMBERS(ephemeral_uid, stream_id)

    Uid ephemeral_uid;
    StreamId stream_id;
  };

  void Pack(LoginByUid&& message, ApiPacker& packer);
  void Pack(LoginByAlias&& message, ApiPacker& packer);
};
}  // namespace ae

#endif  // AETHER_METHODS_WORK_SERVER_API_LOGIN_API_H_
