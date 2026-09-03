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

#include "browser_ping_pong.h"

// Emscripten entry: Module factory returns after main(). The JS UI drives
// configure/start via EMSCRIPTEN_KEEPALIVE exports. Cooperative Aether
// updates are scheduled from BrowserPingPongApp after Construct.
int main() {
  return 0;
}
