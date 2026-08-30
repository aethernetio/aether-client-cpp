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

#ifndef AETHER_WIFI_WIFI_PROBE_CONFIG_H_
#define AETHER_WIFI_WIFI_PROBE_CONFIG_H_

#include <cstdint>

#include "aether/config.h"

namespace ae {

// Reliability thresholds for gateway ICMP profile probes (configurable).
#ifndef AE_WIFI_PROBE_ICMP_BATCH
#  define AE_WIFI_PROBE_ICMP_BATCH 5
#endif
#ifndef AE_WIFI_PROBE_ICMP_PASS_BATCH
#  define AE_WIFI_PROBE_ICMP_PASS_BATCH 5
#endif
#ifndef AE_WIFI_PROBE_ICMP_EXTEND_BATCH
#  define AE_WIFI_PROBE_ICMP_EXTEND_BATCH 5
#endif
#ifndef AE_WIFI_PROBE_ICMP_ACCEPT_NUM
#  define AE_WIFI_PROBE_ICMP_ACCEPT_NUM 9
#endif
#ifndef AE_WIFI_PROBE_ICMP_ACCEPT_DEN
#  define AE_WIFI_PROBE_ICMP_ACCEPT_DEN 10
#endif

#ifndef AE_WIFI_PROBE_PRE_DELAYS_MS
// Conservative → aggressive. Terminated by 0xFFFF.
#  define AE_WIFI_PROBE_PRE_DELAYS_MS \
    { 300, 200, 100, 50, 25, 10, 0, 0xFFFF }
#endif

#ifndef AE_WIFI_PROBE_POST_DELAYS_MS
#  define AE_WIFI_PROBE_POST_DELAYS_MS \
    { 300, 200, 100, 50, 25, 10, 0, 0xFFFF }
#endif

#ifndef AE_WIFI_PROBE_DEFERRED_NEXT_RX_DELAY_MS
#  define AE_WIFI_PROBE_DEFERRED_NEXT_RX_DELAY_MS 10000
#endif

#ifndef AE_WIFI_PROBE_DEFERRED_NEXT_RX_WINDOW_MS
#  define AE_WIFI_PROBE_DEFERRED_NEXT_RX_WINDOW_MS 2000
#endif

#ifndef AE_WIFI_PROBE_DHCP_LEASE_DEFAULT_S
#  define AE_WIFI_PROBE_DHCP_LEASE_DEFAULT_S (12 * 60 * 60)
#endif

#ifndef AE_WIFI_PROBE_HOT_FAIL_BEFORE_CANONICAL
#  define AE_WIFI_PROBE_HOT_FAIL_BEFORE_CANONICAL 2
#endif

#ifndef AE_WIFI_PROBE_SERVER_RESULT_TTL_S
#  define AE_WIFI_PROBE_SERVER_RESULT_TTL_S 120
#endif

}  // namespace ae

#endif  // AETHER_WIFI_WIFI_PROBE_CONFIG_H_
