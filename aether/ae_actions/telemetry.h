/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_AE_ACTIONS_TELEMETRY_H_
#define AETHER_AE_ACTIONS_TELEMETRY_H_

#include "aether/config.h"
#if AE_TELE_ENABLED
#  define TELEMETRY_ENABLED 1

#  include <optional>

#  include "aether/ae_context.h"
#  include "aether/stream_api/istream.h"
#  include "aether/cloud_connections/cloud_subscription.h"
#  include "aether/cloud_connections/cloud_server_connections.h"

#  include "aether/work_cloud_api/telemetric.h"

namespace ae {
class Aether;

class Telemetry {
 public:
  Telemetry(AeContext const& ae_context,
            CloudServerConnections& cloud_connection);

  AE_CLASS_NO_COPY_MOVE(Telemetry)

  void SendTelemetry();

 private:
  void OnRequestTelemetry(std::size_t server_priority);
  std::optional<Telemetric> CollectTelemetry(StreamInfo const& stream_info);

  AeContext ae_context_;
  CloudServerConnections* cloud_connection_;

  CloudEventListener telemetry_request_sub_;
  std::optional<std::size_t> request_for_server_;
};
}  // namespace ae
#endif
#endif  // AETHER_AE_ACTIONS_TELEMETRY_H_
