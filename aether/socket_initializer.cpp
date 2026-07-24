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

#include "aether/socket_initializer.h"

#if defined WIN32
#  include <winsock2.h>

#  include "aether/tele.h"
#endif

namespace ae {
#if defined WIN32
SocketInitializer::SocketInitializer() : initialized_{false} {
  // init winsockets
  static constexpr auto WIN_SOCK_VERSION = MAKEWORD(2, 2);
  WSADATA wsaData;
  if (auto res = WSAStartup(WIN_SOCK_VERSION, &wsaData); res != 0) {
    AE_TELED_ERROR("WSAStartup returns {}", res);
    assert(false);
    return;
  }
  initialized_ = true;
}

SocketInitializer::~SocketInitializer() {
  /* SO says it's safe to call WSACleanup for each sucssessfull WSAStartup
   * https://stackoverflow.com/questions/1869689/is-it-possible-to-tell-if-wsastartup-has-been-called-in-a-process
   */
  if (initialized_) {
    WSACleanup();
  }
}
#endif
}  // namespace ae
