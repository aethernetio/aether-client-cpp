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
#include <iostream>
#include <string_view>

#include "aether/all.h"

#define AE_EXAMPLE_LORA_MODULE 0
#define AE_EXAMPLE_MODEM 0

#if defined ESP_PLATFORM
#  define AE_EXAMPLE_ESP_WIFI 1
#else
#  define AE_EXAMPLE_ETHERNET 1
#endif

// IWYU pragma: begin_keeps
#include "../common/aether_construct_esp_wifi.h"
#include "../common/aether_construct_ethernet.h"
#include "../common/aether_construct_lora_module.h"
#include "../common/aether_construct_modem.h"
// IWYU pragma: end_keeps

namespace ae::examples {

static constexpr inline auto kParentUid =
    ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");

constexpr SafeStreamConfig kSafeStreamConfig{
    .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
    .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
    .max_repeat_count = 10,
    .wait_ack_timeout = std::chrono::seconds{5},
    .send_ack_timeout = std::chrono::seconds{0},
    .send_repeat_timeout = std::chrono::seconds{2},
};
}  // namespace ae::examples

int AetherCloudExample() {
  /**
   * Construct a main aether application class.
   * It's include a Domain and Aether instances accessible by getter methods.
   * It has Update, WaitUntil, Exit, IsExit, ExitCode methods to integrate it
   * in your update loop. Also it has action context protocol implementation
   * \see Action. To configure its creation \see AetherAppContext.
   */
  auto aether_app = ae::examples::construct_aether_app();

  // Clients message exchange state
  int received_count = 0;
  int confirmed_count = 0;

  using namespace std::string_view_literals;
  auto messages = std::array{
      "Hello, it's me"sv,
      "I was wondering if, after all these years, you'd like to meet"sv,
      "To go over everything"sv,
      "They say that time's supposed to heal ya"sv,
      "But I ain't done much healin'"sv,
      "Hello, can you hear me?"sv,
      "I'm in California dreaming about who we used to be"sv,
      "When we were younger and free"sv,
      "I've forgotten how it felt before the world fell at our feet"sv};

  /**
   * Start clients selection or registration.
   * Clients might be loaded from data storage saved during previous run
   * or Registered if not found.
   */
  ae::Client::ptr client_a;
  ae::Client::ptr client_b;

  /**
   * Clients uses message streams to exchange data
   */
  std::unique_ptr<ae::ByteIStream> receiver_stream;
  std::unique_ptr<ae::ByteIStream> sender_stream;

  auto pipeline =
      ae::ex::when_all(ae::ex::action_wait(aether_app->aether()->SelectClient(
                           ae::examples::kParentUid, "A")),
                       ae::ex::action_wait(aether_app->aether()->SelectClient(
                           ae::examples::kParentUid, "B"))) |
      // Both client's registered
      ae::ex::then([&](auto const& a, auto const& b) noexcept {
        client_a = a;
        client_b = b;

        // setup connectivity timings
        using namespace std::chrono_literals;
        client_a->connectivity_policy()->ResetRxTimings();
        client_b->connectivity_policy()->ResetRxTimings();

        client_a->connectivity_policy()
            ->ConfigureRxTimings(ae::RequestPolicy::All{})
            .ForPriority<0>(ae::RxTimingConf::Every(5s))
#if AE_CLOUD_MAX_SERVER_CONNECTIONS >= 3
            .ForPriority<1>(ae::RxTimingConf::Every(10s))
            .ForPriority<2>(ae::RxTimingConf::Every(20s))
#endif
            ;

        client_b->connectivity_policy()
            ->ConfigureRxTimings(ae::RequestPolicy::All{})
            .ForPriority<0>(ae::RxTimingConf::Every(5s))
#if AE_CLOUD_MAX_SERVER_CONNECTIONS >= 3
            .ForPriority<1>(ae::RxTimingConf::Every(10s))
            .ForPriority<2>(ae::RxTimingConf::Every(20s))
#endif
            ;
      }) |
      ae::ex::then([&]() noexcept {
        /**
         * Make required preparation for receiving messages.
         * Subscribe to opening new stream event.
         * Subscribe to receiving message event.
         * Send confirmation to received message.
         */
        client_a->message_stream_manager().new_port_event().Subscribe(
            [&](ae::P2pPortHandle handle) {
              auto dest = handle.destination();
              receiver_stream = ae::make_unique<ae::P2pSafeStream>(
                  *aether_app, ae::examples::kSafeStreamConfig,
                  std::make_shared<ae::P2pStream>(*aether_app, client_a.Load(),
                                                  dest, std::move(handle)));

              receiver_stream->out_data_event().Subscribe(
                  [&](auto const& data) {
                    auto str_msg = std::string_view{
                        reinterpret_cast<const char*>(data.data()),
                        data.size()};
                    ae::Format(std::cout, "~['_']~ Received a message [{}]\n",
                               str_msg);

                    received_count++;
                    auto confirm_msg = ae::Format("confirmed {}", str_msg);
                    auto& response_action = receiver_stream->Write(
                        {confirm_msg.data(),
                         confirm_msg.data() + confirm_msg.size()});
                    response_action.status_event().Subscribe([&](auto status) {
                      if (status == ae::WriteAction::Status::kFail) {
                        ae::Format(std::cerr, "~['_']~ Send response failed\n");
                        aether_app->Exit(1);
                      }
                    });
                  });
            });
      }) |
      ae::ex::then([&]() noexcept {
        /**
         * Make required preparation to send messages.
         * Create a sender to receiver stream.
         * Subscribe to receiving message event for confirmations.
         */
        auto handle =
            client_b->message_stream_manager().CreatePort(client_a->uid());
        sender_stream = ae::make_unique<ae::P2pSafeStream>(
            *aether_app, ae::examples::kSafeStreamConfig,
            std::make_shared<ae::P2pStream>(*aether_app, client_b.Load(),
                                            client_a->uid(),
                                            std::move(handle)));

        sender_stream->out_data_event().Subscribe([&](auto const& data) {
          auto str_response = std::string_view{
              reinterpret_cast<const char*>(data.data()), data.size()};
          ae::Format(std::cout,
                     "~['_']~ Received a response [{}], confirm_count {}\n",
                     str_response, confirmed_count);
          confirmed_count++;
        });
      }) |
      ae::ex::then([&]() noexcept {
        /**
         * Actually send messages
         */
        ae::Format(std::cout, "Send messages\n");

        for (auto const& msg : messages) {
          auto& send_action = sender_stream->Write(
              ae::DataBuffer{std::begin(msg), std::end(msg)});
          send_action.status_event().Subscribe([&](auto status) {
            if (status == ae::WriteAction::Status::kFail) {
              ae::Format(std::cerr, "~['_']~ Send message failed\n");
              aether_app->Exit(1);
            }
          });
        }
      });

  /**
   * Full preparation pipeline is an asynchronous operation defined
   * by stdexec's sender.
   * Make async waiter to wait till it completes.
   */
  auto waiter = ae::ex::AsyncWaiter{ae::AeContext{*aether_app},
                                    std::move(pipeline), [&](auto res) {
                                      if (!res || res->IsErr()) {
                                        aether_app->Exit(1);
                                      }
                                    }};
  /**
   * Application loop.
   * All the asynchronous actions are updated on this loop.
   * WaitUntil either waits until the next selected time or some action
   * triggers new event.
   */
  while (!aether_app->IsExited()) {
    ae::Format(std::cout,
               "~['_']~ Wait cloud test received_count={} confirmed_count={}\n",
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
