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

#include "aether/types/uid.h"
#include "aether/types/data_buffer.h"
#include "aether/api_protocol/api_method.h"

namespace ae {
class LoginApi : public ApiClass {
 public:
  explicit LoginApi(ProtocolContext& protocol_context);

  Method<06, void(Uid uid, DataBuffer data)> login_by_uid;
  Method<07, void(Uid alias, DataBuffer data)> login_by_alias;
};
}  // namespace ae

#endif  // AETHER_METHODS_WORK_SERVER_API_LOGIN_API_H_
