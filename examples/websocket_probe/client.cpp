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

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether/aether_app.h"
#include "aether/client_connectivity_policy.h"
#include "aether/clock.h"
#include "aether/global_ids.h"
#include "aether/registration_cloud.h"
#include "aether/server.h"
#include "aether/transport/websocket/websocket.h"

int main() {
  // Explicit WSS-only registration avoids accidentally proving a TCP fallback.
  auto app = ae::AetherApp::Construct(
      ae::AetherAppContext{[]() -> std::unique_ptr<ae::IDomainStorage> {
        return std::make_unique<ae::RamDomainStorage>();
      }}.RegistrationCloudFactory([](ae::AetherAppContext const& context) {
        auto cloud = ae::RegistrationCloud::ptr::Create(
            ae::CreateWith{context.domain()}.with_id(
                ae::GlobalId::kRegistrationCloud),
            context.aether());
        cloud->AddServerSettings(
            ae::Endpoint{{ae::NamedAddr{"dbservice.aethernet.io"}, 9013},
                         ae::Protocol::kWebSocketSecure});
        return ae::Cloud::ptr{std::move(cloud)};
      }));
  ae::Client::ptr client;
  bool selected = false;
  ae::Subscription selection =
      app->aether()
          ->SelectClient(
              ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"),
              "websocket-sdk-probe")
          .result_event()
          .Subscribe([&](auto const& result) {
            selected = true;
            if (result) client = result.value();
          });
  auto deadline = ae::Now() + std::chrono::seconds{90};
  bool connection_started = false;
  while (ae::Now() < deadline) {
    app->Update(ae::Now());
    if (selected && !client.is_valid()) break;
    if (client.is_valid() && !connection_started) {
      client->connectivity_policy()
          .Load()
          ->ConfigureRxTimings()
          .ForAllPriorities(ae::RxTimingConf::Every(std::chrono::seconds{1}));
      // Selection/registration does not create a receive connection by itself.
      // This is normal lazy SDK behavior, not a transport failure.
      (void)client->cloud_connection();
      connection_started = true;
    }
    if (client.is_valid() && client->IsLocallyOnline()) break;
#ifdef __EMSCRIPTEN__
    emscripten_sleep(5);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
#endif
  }
  bool registered = client.is_valid();
  bool online = registered && client->IsLocallyOnline();
  if (registered) {
    std::cout << "registered_uid=" << ae::Format("{}", client->uid()) << '\n';
    for (auto const& [id, entry] : client->cloud().Load()->servers()) {
      auto server = entry.server.Load();
      if (!server) return 2;
      for (auto const& endpoint : server->endpoints) {
        std::cout << "server=" << ae::Format("{}", id)
                  << " protocol=" << static_cast<unsigned>(endpoint.protocol)
                  << " port=" << endpoint.port << '\n';
        if (endpoint.protocol == ae::Protocol::kWebSocket ||
            endpoint.protocol == ae::Protocol::kWebSocketSecure) {
          std::cout << "websocket_endpoint="
                    << ae::WebSocketTransport::Url(endpoint) << '\n';
        }
      }
    }
  }
  std::cout << (registered && online ? "PASS" : "FAIL")
            << " Aether WSS registration registered=" << registered
            << " online=" << online << '\n';
  return registered && online ? 0 : 1;
}
