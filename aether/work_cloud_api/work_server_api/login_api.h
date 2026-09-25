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

#include "aether/api_protocol/api_protocol.h"
#include "aether/crypto/icrypto_provider.h"
#include "aether/types/uid.h"

#include "aether/work_cloud_api/info_ip.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

namespace ae {
class LoginApi : public DeclareApi<LoginApi> {
 public:
  explicit LoginApi(IEncryptProvider& encrypt_provider);

  void LoginByUid(Uid const& uid, SubApi<AuthorizedApi> sub_api);
  void LoginByAlias(Uid const& alias, SubApi<AuthorizedApi> sub_api);
  ApiPromise<InfoIp> GetMyIp();

  API_LIST(METHOD(4, LoginByUid), METHOD(5, LoginByAlias), METHOD(6, GetMyIp))

  AuthorizedApi& authorized_api() { return auth_api_; }

 private:
  IEncryptProvider* encrypt_provider_;
  AuthorizedApi auth_api_;
};
}  // namespace ae

#endif  // AETHER_WORK_CLOUD_API_WORK_SERVER_API_LOGIN_API_H_
