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

#include "aether/types/uid.h"
#include "aether/types/server_id.h"
#include "aether/api_protocol/api_protocol.h"

#include "aether/work_cloud_api/ae_message.h"
#include "aether/work_cloud_api/telemetric.h"
#include "aether/work_cloud_api/cloud_configs.h"

namespace ae {

class AuthorizedApi : public ApiClass {
 public:
  explicit AuthorizedApi(ProtocolContext& protocol_context);

  Method<4, ApiPromise<void>(std::uint64_t next_connect_ms_duration,
                             std::uint64_t rx_window_ms)>
      ping;
  // PingV2 / probe: current_rx_window_ms > 0 for immediate RTT (ChannelStatistics).
  // Deferred: current_rx_window_ms = 0; next_* describe the following RX window.
  Method<43, ApiPromise<void>(std::uint64_t probe_id,
                              std::uint64_t current_rx_window_ms,
                              std::uint64_t next_rx_delay_ms,
                              std::uint64_t next_rx_window_ms)>
      probe_ping;
  Method<44, void(std::uint64_t probe_id, std::uint64_t current_rx_window_ms,
                  std::uint64_t next_rx_delay_ms,
                  std::uint64_t next_rx_window_ms)>
      deferred_probe;
  // 0=unknown/expired, 1=received, 2=not_received (reserved).
  Method<45, ApiPromise<std::uint8_t>(std::uint64_t probe_id)>
      query_probe_result;
  Method<6, void(AeMessage message)> send_message;
  Method<7, void(std::vector<AeMessage> messages)> send_messages;
  Method<11, ApiPromise<void>(Uid uid)> check_access_for_send_message;
  Method<12, void(std::vector<ServerId> sids)> resolver_servers;
  Method<13, void(std::vector<Uid> uids)> resolver_clouds;

  Method<18, void(Telemetric telemetric)> send_telemetry;

  Method<38, void(std::vector<AppliedConfig> configs)> report_applied_config;
};
}  // namespace ae

#endif  // AETHER_WORK_CLOUD_API_WORK_SERVER_API_AUTHORIZED_API_H_
