/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_WORK_CLOUD_API_WORK_SERVER_API_SERVER_API_BY_UID_H_
#define AETHER_WORK_CLOUD_API_WORK_SERVER_API_SERVER_API_BY_UID_H_

#include <cstdint>

#include "aether/api_protocol/api_protocol.h"

namespace ae {

// Wire Date is java.util.Date: signed int64 epoch milliseconds.
// Method IDs match ClientServerApi.adsl.yaml ServerApiByUid.
class ServerApiByUid : public ApiClass {
 public:
  explicit ServerApiByUid(ProtocolContext& protocol_context);

  Method<13, ApiPromise<std::int64_t>()> online_time;
  Method<18, ApiPromise<std::int64_t>()> next_online_time;
};

}  // namespace ae

#endif  // AETHER_WORK_CLOUD_API_WORK_SERVER_API_SERVER_API_BY_UID_H_
