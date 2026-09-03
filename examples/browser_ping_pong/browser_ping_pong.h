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

#ifndef EXAMPLES_BROWSER_PING_PONG_BROWSER_PING_PONG_H_
#define EXAMPLES_BROWSER_PING_PONG_BROWSER_PING_PONG_H_

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// C API / EM_ASM bindings for the static web UI. All pointers are UTF-8
// C strings unless noted. Returned string pointers are valid until the next
// call that returns a string (or until clear/start).

void aether_bpp_configure(char const* profile, char const* gateway_url,
                          char const* transport);
void aether_bpp_start(void);
void aether_bpp_set_remote_uid(char const* uid);
void aether_bpp_connect(void);
void aether_bpp_send_ping(void);
void aether_bpp_start_periodic(int interval_ms);
void aether_bpp_stop(void);
void aether_bpp_clear_profile(void);
void aether_bpp_set_ping_timeout_ms(int timeout_ms);
void aether_bpp_set_payload_bytes(int payload_bytes);

char const* aether_bpp_get_uid(void);
char const* aether_bpp_get_state(void);
char const* aether_bpp_get_stats_json(void);
char const* aether_bpp_get_profile(void);

#ifdef __cplusplus
}
#endif

#endif  // EXAMPLES_BROWSER_PING_PONG_BROWSER_PING_PONG_H_
