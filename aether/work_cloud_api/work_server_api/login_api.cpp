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

#include "aether/work_cloud_api/work_server_api/login_api.h"

#include "aether/tele.h"

namespace ae {
LoginApi::LoginApi(IEncryptProvider& encrypt_provider)
    : encrypt_provider_{&encrypt_provider}, auth_api_{} {}

void LoginApi::LoginByUid(Uid const& uid, SubApi<AuthorizedApi> sub_api) {
  auto auth_data = std::move(sub_api)(auth_api_, client_context());
  AE_TELED_DEBUG("Login by uid {} data [{}]", uid, auth_data);
  auto enc_data = encrypt_provider_->Encrypt(auth_data);
  ClientMethod<&LoginApi::LoginByAlias>(uid, std::move(enc_data));
}

void LoginApi::LoginByAlias(Uid const& alias, SubApi<AuthorizedApi> sub_api) {
  auto auth_data = std::move(sub_api)(auth_api_, client_context());
  AE_TELED_DEBUG("Login by alias {} data [{}]", alias, auth_data);
  auto enc_data = encrypt_provider_->Encrypt(auth_data);
  ClientMethod<&LoginApi::LoginByAlias>(alias, std::move(enc_data));
}

ApiPromise<InfoIp> LoginApi::GetMyIp() {
  return ClientMethod<&LoginApi::GetMyIp>();
}
}  // namespace ae
