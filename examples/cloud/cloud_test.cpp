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

#include <chrono>
#include <cstdint>

#include "aether/all.h"
#include "aether/adapters/modem_adapter.h"
#include "aether/adapters/esp32_wifi.h"

static constexpr std::string_view kWifiSsid = "Test1234";
static constexpr std::string_view kWifiPass = "Test1234";

namespace ae::cloud_test {
constexpr ae::SafeStreamConfig kSafeStreamConfig{
    std::numeric_limits<std::uint16_t>::max(),                // buffer_capacity
    (std::numeric_limits<std::uint16_t>::max() / 2) - 1,      // window_size
    (std::numeric_limits<std::uint16_t>::max() / 2) - 1 - 1,  // max_data_size
    10,                              // max_repeat_count
    std::chrono::milliseconds{600},  // wait_confirm_timeout
    {},                              // send_confirm_timeout
    std::chrono::milliseconds{400},  // send_repeat_timeout
};
}  // namespace ae::cloud_test

int AetherCloudExample() {
  ae::SerialInit serial_init = {"COM47", 115200}; // Thingy91x
  //ae::SerialInit serial_init = {"COM17", 115200}; // Sim7070g

  ae::ModemInit modem_init{
      serial_init,                  // Serial port
      {1, 1, 1, 1},                 // Pin code
      false,                        // Use pin
      ae::kModemMode::kModeNbIot,   // Modem mode Thingy91x
      //ae::kModemMode::kModeAuto,      // Modem mode Sim7070g
      "25001",                      // Operator code Thingy91x
      //"25020",                      // Operator code Sim7070g
      "",                           // Operator long name
      "iot",                        // APN
      "",                           // APN user
      "",                           // APN pass
      ae::kAuthType::kAuthTypeNone  // Auth type
  };

  /**
   * Construct a main aether application class.
   * It's include a Domain and Aether instances accessible by getter methods.
   * It has Update, WaitUntil, Exit, IsExit, ExitCode methods to integrate it in
   * your update loop.
   * Also it has action context protocol implementation \see Action.
   * To configure its creation \see AetherAppContext.
   */
  auto aether_app = ae::AetherApp::Construct(
      ae::AetherAppContext{}
#if defined AE_DISTILLATION
          .AdapterFactory(
              [modem_init](ae::AetherAppContext const& context) {
#  if defined ESP32_WIFI_ADAPTER_ENABLED
                auto adapter = context.domain().CreateObj<ae::Esp32WifiAdapter>(
                    ae::GlobalId::kEsp32WiFiAdapter, context.aether(),
                    context.poller(), context.dns_resolver(),
                    std::string(kWifiSsid), std::string(kWifiPass));
#  elif defined MODEM_ADAPTER_ENABLED
                auto adapter = context.domain().CreateObj<ae::ModemAdapter>(
                    ae::GlobalId::kModemAdapter, context.aether(),
                    context.poller(), context.dns_resolver(), modem_init);
#  else
                auto adapter = context.domain().CreateObj<ae::EthernetAdapter>(
                    ae::GlobalId::kEthernetAdapter, context.aether(),
                    context.poller(), context.dns_resolver());
#  endif
                return adapter;
              })
#endif
  );

  /**
   * Start clients selection or registration.
   * Clients might be loaded from data storage saved during previous run
   * or Registered if not found.
   */
  ae::Client::ptr client_a;
  ae::Client::ptr client_b;

  auto select_client_a = aether_app->aether()->SelectClient(
      ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"), 0);
  select_client_a->ResultEvent().Subscribe(
      [&](auto const& action) { client_a = action.client(); });
  select_client_a->ErrorEvent().Subscribe(
      [&](auto const&) { aether_app->Exit(1); });

  aether_app->WaitAction(select_client_a);

  auto select_client_b = aether_app->aether()->SelectClient(
      ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"), 1);
  select_client_b->ResultEvent().Subscribe(
      [&](auto const& action) { client_b = action.client(); });
  select_client_b->ErrorEvent().Subscribe(
      [&](auto const&) { aether_app->Exit(1); });

  aether_app->WaitAction(select_client_b);

  // wait till clients selected
  assert(client_a && client_b);

  // Make clients messages exchange.
  int received_count = 0;
  int confirmed_count = 0;

  /**
   * Make required preparation for receiving messages.
   * Subscribe to opening new stream event.
   * Subscribe to receiving message event.
   * Send confirmation to received message.
   */
  std::unique_ptr<ae::ByteIStream> receiver_stream;
  client_a->client_connection()->new_stream_event().Subscribe(
      [&](auto uid, auto raw_stream) {
        receiver_stream = ae::make_unique<ae::P2pSafeStream>(
            *aether_app, ae::cloud_test::kSafeStreamConfig,
            ae::make_unique<ae::P2pStream>(*aether_app, client_a, uid,
                                           std::move(raw_stream)));

        receiver_stream->out_data_event().Subscribe([&](auto const& data) {
          auto str_msg = std::string(reinterpret_cast<const char*>(data.data()),
                                     data.size());
          AE_TELED_DEBUG("Received a message [{}]", str_msg);
          received_count++;
          auto confirm_msg = std::string{"confirmed "} + str_msg;
          auto response_action = receiver_stream->Write(
              {confirm_msg.data(), confirm_msg.data() + confirm_msg.size()});
          response_action->ErrorEvent().Subscribe([&](auto const&) {
            AE_TELED_ERROR("Send response failed");
            aether_app->Exit(1);
          });
        });
      });

  /**
   * Make required preparation to send messages.
   * Create a sender to receiver stream.
   * Subscribe to receiving message event for confirmations.
   */
  auto sender_stream = ae::make_unique<ae::P2pSafeStream>(
      *aether_app, ae::cloud_test::kSafeStreamConfig,
      ae::make_unique<ae::P2pStream>(*aether_app, client_b, client_a->uid()));

  sender_stream->out_data_event().Subscribe([&](auto const& data) {
    auto str_response =
        std::string(reinterpret_cast<const char*>(data.data()), data.size());
    AE_TELED_DEBUG("Received a response [{}], confirm_count {}", str_response,
                   confirmed_count);
    confirmed_count++;
  });

  AE_TELED_INFO("Send messages");
  auto messages = std::array<std::string, 9>{
      "Hello, it's me",
      "I was wondering if, after all these years, you'd like to meet",
      "To go over everything",
      "They say that time's supposed to heal ya",
      "But I ain't done much healin'",
      "Hello, can you hear me?",
      "I'm in California dreaming about who we used to be",
      "When we were younger and free",
      "I've forgotten how it felt before the world fell at our feet"};

  for (auto const& msg : messages) {
    auto send_action =
        sender_stream->Write(ae::DataBuffer{std::begin(msg), std::end(msg)});
    send_action->ErrorEvent().Subscribe([&](auto const&) {
      AE_TELED_ERROR("Send message failed");
      aether_app->Exit(1);
    });
  }

  /**
   * Application loop.
   * All the asynchronous actions are updated on this loop.
   * WaitUntil either waits until the next selected time or some action
   * triggers new event.
   */
  while (!aether_app->IsExited()) {
    AE_TELED_DEBUG("Wait cloud test received_count={} confirmed_count={}",
                   received_count, confirmed_count);
    if ((received_count == messages.size()) &&
        (confirmed_count == messages.size())) {
      aether_app->Exit(0);
      continue;
    }
    // Wait for next event or timeout
    auto current_time = ae::Now();
    auto next_time = aether_app->Update(current_time);
    aether_app->WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
  }

  return aether_app->ExitCode();
}
