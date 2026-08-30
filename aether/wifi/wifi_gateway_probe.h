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

#ifndef AETHER_WIFI_WIFI_GATEWAY_PROBE_H_
#define AETHER_WIFI_WIFI_GATEWAY_PROBE_H_

#include <cstdint>

#include "aether/wifi/wifi_probe_state.h"

namespace ae {

// ICMP Echo to the default gateway. RTT is intentionally discarded.
struct WifiGatewayProbeResult {
  WifiProbeIcmpStats stats{};
  bool icmp_supported{true};
  bool network_ready{false};
};

#if defined(ESP_PLATFORM)
// Blocking ICMP batch using ESP-IDF ping. Does not store RTT.
WifiGatewayProbeResult WifiGatewayIcmpProbe(std::uint32_t gateway_be,
                                            std::uint16_t count);
#else
inline WifiGatewayProbeResult WifiGatewayIcmpProbe(std::uint32_t,
                                                   std::uint16_t count) {
  WifiGatewayProbeResult r;
  r.stats.sent = count;
  r.stats.received = count;
  r.network_ready = true;
  return r;
}
#endif

}  // namespace ae

#endif  // AETHER_WIFI_WIFI_GATEWAY_PROBE_H_
