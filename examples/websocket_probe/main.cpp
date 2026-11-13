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

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

#include "aether/ae_context.h"
#include "aether/clock.h"
#include "aether/transport/websocket/websocket.h"

namespace ae::websocket_probe {
struct Context final : Env {
  void* find_component(EnvId id) noexcept override {
    if (id == EnvTypeId<TaskScheduler>::value) return &scheduler;
    if (id == EnvTypeId<EventSystem>::value) return &events;
    return nullptr;
  }
  TaskScheduler scheduler;
  EventSystem events;
};
int Run() {
  Context context;
  bool sent = false;
  bool received = false;
  WebSocketTransport transport{
      context, Endpoint{{NamedAddr{"dbservice.aethernet.io"}, 9013},
                        Protocol::kWebSocketSecure}};
  Subscription message =
      transport.out_data_event().Subscribe([&](DataBuffer const& data) {
        // Existing registration-root API: signed public key notification.
        if (data.size() == 99 && data[0] == 70) received = true;
        std::cout << "registration_response_bytes=" << data.size() << '\n';
      });
  auto deadline = Now() + std::chrono::seconds{20};
  while (Now() < deadline && !received) {
    context.scheduler.Update();
    if (!sent && transport.stream_info().is_writable) {
      transport.Write(DataBuffer{3, 1});
      sent = true;
    }
    if (transport.stream_info().link_state == LinkState::kLinkError) break;
#ifdef __EMSCRIPTEN__
    emscripten_sleep(5);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
#endif
  }
  std::cout << (received ? "PASS" : "FAIL") << " native Aether WSS framing\n";
  return received ? 0 : 1;
}
}  // namespace ae::websocket_probe
int main() { return ae::websocket_probe::Run(); }
