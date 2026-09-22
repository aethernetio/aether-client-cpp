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

#ifndef AETHER_WORK_CLOUD_API_WORK_SERVER_API_AUTHORIZED_API_H_
#define AETHER_WORK_CLOUD_API_WORK_SERVER_API_AUTHORIZED_API_H_

#include <vector>

#include "aether/api_protocol/api_protocol.h"
#include "aether/types/server_id.h"
#include "aether/types/uid.h"

#include "aether/work_cloud_api/ae_message.h"
#include "aether/work_cloud_api/cloud_configs.h"
#include "aether/work_cloud_api/telemetric.h"

namespace ae {

class AuthorizedApi : public DeclareApi<AuthorizedApi> {
 public:
  AuthorizedApi() = default;

  ApiPromise<void> Ping(std::uint64_t next_connect_ms_duration,
                        std::uint64_t rx_window_ms);
  void SendMessage(AeMessage const& message);
  void SendMessages(std::vector<AeMessage> const& messages);
  ApiPromise<void> CheckAccessForSendMessage(Uid const& uid);
  void ResolveServer(std::vector<ServerId> const& sids);
  void ResolveClouds(std::vector<Uid> const& uids);

  void SendTelemetry(Telemetric const& telemetric);

  void ReportAppliedConfigs(std::vector<AppliedConfig> const& configs);

  API_LIST(METHOD(4, Ping), METHOD(6, SendMessage), METHOD(7, SendMessages),
           METHOD(11, CheckAccessForSendMessage), METHOD(12, ResolveServer),
           METHOD(13, ResolveClouds), METHOD(18, SendTelemetry),
           METHOD(38, ReportAppliedConfigs))
};
}  // namespace ae

#endif  // AETHER_WORK_CLOUD_API_WORK_SERVER_API_AUTHORIZED_API_H_
