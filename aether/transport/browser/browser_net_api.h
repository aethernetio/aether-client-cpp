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

#ifndef AETHER_TRANSPORT_BROWSER_BROWSER_NET_API_H_
#define AETHER_TRANSPORT_BROWSER_BROWSER_NET_API_H_

#include <cstddef>
#include <cstdint>

#ifdef __EMSCRIPTEN__

extern "C" {

// Generation is uint32_t on the JS ABI (Emscripten makeDynCall forbids "j").

using AeBrowserWsOpenCb = void (*)(void* user_data, std::uint32_t generation);
using AeBrowserWsMessageCb = void (*)(void* user_data, std::uint32_t generation,
                                      std::uint8_t const* data, int size);
using AeBrowserWsCloseCb = void (*)(void* user_data, std::uint32_t generation);
using AeBrowserWsErrorCb = void (*)(void* user_data, std::uint32_t generation);

int ae_browser_ws_open(char const* url, void* user_data,
                       std::uint32_t generation, AeBrowserWsOpenCb on_open,
                       AeBrowserWsMessageCb on_message,
                       AeBrowserWsCloseCb on_close, AeBrowserWsErrorCb on_error);
int ae_browser_ws_send(int handle, void const* data, int size);
int ae_browser_ws_buffered_amount(int handle);
void ae_browser_ws_close(int handle);

using AeBrowserHttpOkCb = void (*)(void* user_data, std::uint32_t generation,
                                   char const* session_id);
using AeBrowserHttpErrCb = void (*)(void* user_data, std::uint32_t generation);
using AeBrowserHttpSendOkCb = void (*)(void* user_data,
                                       std::uint32_t generation);
using AeBrowserHttpDataCb = void (*)(void* user_data, std::uint32_t generation,
                                     std::uint8_t const* data, int size);

int ae_browser_http_connect(char const* url, void const* body, int body_size,
                            void* user_data, std::uint32_t generation,
                            AeBrowserHttpOkCb on_ok, AeBrowserHttpErrCb on_err);
int ae_browser_http_send(char const* url, void const* data, int size,
                         void* user_data, std::uint32_t generation,
                         AeBrowserHttpSendOkCb on_ok,
                         AeBrowserHttpErrCb on_err);
int ae_browser_http_receive(int handle, char const* url, void* user_data,
                            std::uint32_t generation, AeBrowserHttpDataCb on_data,
                            AeBrowserHttpErrCb on_err);
void ae_browser_http_close(int handle, char const* url);

}  // extern "C"

#endif  // __EMSCRIPTEN__

#endif  // AETHER_TRANSPORT_BROWSER_BROWSER_NET_API_H_
