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

#ifndef AETHER_WORK_CLOUD_API_WORK_SERVER_API_LOGIN_API_H_
#define AETHER_WORK_CLOUD_API_WORK_SERVER_API_LOGIN_API_H_

#include "aether/types/uid.h"
#include "aether/types/data_buffer.h"
#include "aether/crypto/icrypto_provider.h"
#include "aether/api_protocol/api_protocol.h"

#include "aether/work_cloud_api/work_server_api/authorized_api.h"

namespace ae {
class LoginApi : public ApiClass {
  class LoginProc {
   public:
    explicit LoginProc(LoginApi& login_api) : login_api_{&login_api} {}

    auto operator()(Uid uid, SubApi<AuthorizedApi> sub_api) {
      auto def_proc = DefaultArgProc{};
      return def_proc(uid, login_api_->Encrypt(sub_api(login_api_->auth_api_)));
    }

   private:
    LoginApi* login_api_;
  };

 public:
  explicit LoginApi(ProtocolContext& protocol_context,
                    ActionContext action_context,
                    IEncryptProvider& encrypt_provider);

  Method<3, ApiPromisePtr<std::uint64_t>()> get_time_utc;
  Method<4, void(Uid uid, SubApi<AuthorizedApi> sub_api), LoginProc>
      login_by_uid;
  Method<5, void(Uid alias, SubApi<AuthorizedApi> sub_api), LoginProc>
      login_by_alias;

  AuthorizedApi& authorized_api() { return auth_api_; }

 private:
  DataBuffer Encrypt(DataBuffer const& data);

  IEncryptProvider* encrypt_provider_;
  AuthorizedApi auth_api_;
};
}  // namespace ae

#endif  // AETHER_WORK_CLOUD_API_WORK_SERVER_API_LOGIN_API_H_
