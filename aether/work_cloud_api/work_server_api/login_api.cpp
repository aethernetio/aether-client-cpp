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

#include "aether/tele/tele.h"

namespace ae {
LoginApi::LoginApi(ProtocolContext& protocol_context,
                   ActionContext action_context,
                   IEncryptProvider& encrypt_provider)
    : ApiClass{protocol_context},
      get_time_utc{protocol_context, action_context},
      login_by_uid{protocol_context, LoginProc{*this}},
      login_by_alias{protocol_context, LoginProc{*this}},
      encrypt_provider_{&encrypt_provider},
      auth_api_{protocol_context, action_context} {}

DataBuffer LoginApi::Encrypt(DataBuffer const& data) {
  AE_TELED_DEBUG("Login api data {}", data);
  auto enc_data = encrypt_provider_->Encrypt(data);
  AE_TELED_DEBUG("Login api encrypted size {}, {}", enc_data.size(), enc_data);
  return enc_data;
}

}  // namespace ae
