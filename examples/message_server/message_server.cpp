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

#include <iostream>
#include <map>
#include <memory>
#include <string_view>

#if defined __unix__ || defined _WIN32
#  include <signal.h>
#endif

#include "aether/all.h"

// IWYU pragma: begin_keeps
// common aether app construction logic for different scenarios
#if defined ESP_PLATFORM
#  define AE_EXAMPLE_ESP_WIFI 1
#else
#  define AE_EXAMPLE_ETHERNET 1
#endif

#include "aether_construct.h"
#include "aether_construct_esp_wifi.h"
#include "aether_construct_ethernet.h"
#include "aether_construct_lora_module.h"
#include "aether_construct_modem.h"
// IWYU pragma: end_keeps

void SetInterruptHandler([[maybe_unused]] ae::AetherApp& app) {
#if defined __unix__ || defined _WIN32
  static void* app_ptr;
  app_ptr = &app;
  auto handler = +[](int) {
    std::cout << "\n >>> Interrupted, exiting...\n\n";
    static_cast<ae::AetherApp*>(app_ptr)->Exit(0);
  };
  signal(SIGINT, handler);
#endif
}

static constexpr auto kParentUid =
    ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");

void MessageReceived(ae::Uid sender, ae::DataBuffer const& message) {
  ae::Format(
      std::cout,
      "\n >>> Received message from {}\n >>> blob: {}\n >>> as text: {}\n\n",
      sender, message,
      std::string_view{reinterpret_cast<char const*>(message.data()),
                       message.size()});
}

void SubscribeToMessages(ae::Client::ptr const& client,
                         ae::AeContext const& context,
                         std::map<ae::Uid, std::shared_ptr<ae::P2pStream>>&
                             client_streams) noexcept {
  auto client_ptr = client.Load();
  assert(client_ptr);

  // listen for new port open events from the other clients
  client_ptr->message_stream_manager().new_port_event().Subscribe(
      [&, c_ = ae::Ptr<ae::Client>(client_ptr),
       ctx_ = context](ae::P2pPortHandle&& p2p_handle) {
        auto dest = p2p_handle.destination();
        // insert new stream into client streams map
        auto [stream, _] = client_streams.insert_or_assign(
            dest, std::make_shared<ae::P2pStream>(ctx_, c_, dest,
                                                  std::move(p2p_handle)));
        // subscribe to out data event for receiving messages
        stream->second->out_data_event().Subscribe(
            [dest](ae::DataBuffer const& data) {
              MessageReceived(dest, data);
            });
      });
}

int MessageServerExample() {
  auto aether_app = ae::examples::construct_aether_app();
  SetInterruptHandler(*aether_app);

  std::map<ae::Uid, std::shared_ptr<ae::P2pStream>> client_streams;

  // build async initialization pipeline
  auto pipeline =
      ae::ex::action_wait(
          aether_app->aether()->SelectClient(kParentUid, "message_server")) |
      ae::ex::then([&](ae::Client::ptr const& client) noexcept {
        ae::Format(std::cout,
                   "\n >>> Message server client selected\n >>> Uid: {}\n\n",
                   client->uid());
        SubscribeToMessages(client, *aether_app, client_streams);
      });

  // wait for the initialization to complete
  auto waiter = ae::ex::AsyncWaiter{
      ae::AeContext{*aether_app}, std::move(pipeline),
      [&](auto&& res) noexcept {
        if (res && res->IsOk()) {
          std::cout << "\n >>> Message server is started\n\n";
        } else {
          std::cerr << "\n >>> Message server startup failed\n\n";
          aether_app->Exit(1);
        }
      }};

  // run main app logic loop until the app is exited
  while (!aether_app->IsExited()) {
    auto wait_time = aether_app->Update(ae::Now());
    aether_app->WaitUntil(wait_time);
  }
  return aether_app->ExitCode();
}
