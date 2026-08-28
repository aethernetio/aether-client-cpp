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

#ifndef AETHER_WORK_CLOUD_API_CLIENT_TIMING_H_
#define AETHER_WORK_CLOUD_API_CLIENT_TIMING_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

namespace ae {

// Wire DTO for AuthorizedApi.get_client_timing. Field order matches ADSL:
// nextPingDeltaMs then lastConnectDeltaMs.
struct ClientTiming {
  AE_REFLECT_MEMBERS(next_ping_delta_ms, last_connect_delta_ms)

  std::int64_t next_ping_delta_ms{};
  std::int64_t last_connect_delta_ms{};
};

}  // namespace ae

#endif  // AETHER_WORK_CLOUD_API_CLIENT_TIMING_H_
