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

#include "aether/work_cloud_api/work_server_api/authorized_api.h"

namespace ae {
ApiPromise<void> AuthorizedApi::Ping(std::uint64_t next_connect_ms_duration,
                                     std::uint64_t rx_window_ms) {
  return ClientMethod<&AuthorizedApi::Ping>(next_connect_ms_duration,
                                            rx_window_ms);
}
void AuthorizedApi::SendMessage(AeMessage const& message) {
  ClientMethod<&AuthorizedApi::SendMessage>(message);
}
void AuthorizedApi::SendMessages(std::vector<AeMessage> const& messages) {
  ClientMethod<&AuthorizedApi::SendMessages>(messages);
}
ApiPromise<void> AuthorizedApi::CheckAccessForSendMessage(Uid const& uid) {
  return ClientMethod<&AuthorizedApi::CheckAccessForSendMessage>(uid);
}
void AuthorizedApi::ResolveServer(std::vector<ServerId> const& sids) {
  ClientMethod<&AuthorizedApi::ResolveServer>(sids);
}
void AuthorizedApi::ResolveClouds(std::vector<Uid> const& uids) {
  ClientMethod<&AuthorizedApi::ResolveClouds>(uids);
}

void AuthorizedApi::SendTelemetry(Telemetric const& telemetric) {
  ClientMethod<&AuthorizedApi::SendTelemetry>(telemetric);
}

void AuthorizedApi::ReportAppliedConfigs(
    std::vector<AppliedConfig> const& configs) {
  ClientMethod<&AuthorizedApi::ReportAppliedConfigs>(configs);
}
}  // namespace ae
