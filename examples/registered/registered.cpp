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

#include <vector>

#include "aether/aether.h"
#include "aether/common.h"
#include "aether/global_ids.h"
#include "aether/port/tele_init.h"
#include "aether/literal_array.h"
#include "aether/crypto/sign.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"
#include "aether/ae_actions/registration/registration.h"

#include "aether/obj/ptr.h"
#include "aether/client_connections/client_connection.h"
#include "aether/stream_api/safe_stream/safe_stream_config.h"

#if (defined(__linux__) || defined(__unix__) || defined(__FreeBSD__) || \
     defined(_WIN64) || defined(_WIN32) || defined(__APPLE__))
#  include "aether/port/file_systems/file_system_header.h"
#  include "aether/port/file_systems/file_system_std.h"
#  include "aether/adapters/ethernet.h"
#  include "aether/adapters/register_wifi.h"
#elif (defined(ESP_PLATFORM))
#  include "aether/port/file_systems/file_system_header.h"
#  include "aether/port/file_systems/file_system_ram.h"
#  include "aether/port/file_systems/file_system_spifs_v1.h"
#  include "aether/port/file_systems/file_system_spifs_v2.h"
#  include "aether/adapters/esp32_wifi.h"
#endif

#include "aether/tele/tele.h"

static constexpr int wait_time = 100;

namespace ae {

static Aether::ptr LoadAether(Domain& domain) {
  Aether::ptr aether;
  aether.SetId(GlobalId::kAether);
  domain.LoadRoot(aether);
  assert(aether);
  return aether;
}

}  // namespace ae

int AetherRegistered();

int AetherRegistered() {
  ae::TeleInit::Init();
  {
    AE_TELE_ENV();
    AE_TELE_INFO("Started");
    ae::Registry::Log();
  }

  auto fs = ae::FileSystemHeaderFacility{};

  ae::Domain domain{ae::ClockType::now(), fs};
  ae::Aether::ptr aether = ae::LoadAether(domain);
  ae::TeleInit::Init(aether);

  ae::Adapter::ptr adapter{domain.LoadCopy(aether->adapter_factories.front())};

  int receive_count = 0;
  int confirm_count = 0;
  std::vector<ae::Client::ptr> clients{};
  std::vector<std::string> messages{};
  std::vector<ae::Ptr<ae::ClientConnection>> receiver_connections{};
  std::vector<ae::Ptr<ae::P2pSafeStream>> receiver_streams{};
  std::vector<ae::Ptr<ae::P2pSafeStream>> sender_streams{};
  std::vector<ae::Subscription> receiver_subscriptions{};
  std::vector<ae::Subscription> stream_subscriptions{};
  std::vector<ae::Subscription> sender_subscriptions{};

  ae::SafeStreamConfig config;
  config.buffer_capacity = std::numeric_limits<std::uint16_t>::max();
  config.max_repeat_count = 4;
  config.window_size = (config.buffer_capacity / 2) - 1;
  config.wait_confirm_timeout = std::chrono::milliseconds{200};
  config.send_confirm_timeout = {};
  config.send_repeat_timeout = std::chrono::milliseconds{200};
  config.max_data_size = config.window_size - 1;

  // Load clients
  for (std::size_t i{0}; i < aether->clients().size(); i++) {
    clients.insert(clients.begin() + i, aether->clients()[i]);
    auto msg_str = std::string("Test message for client ") + std::to_string(i);
    messages.insert(messages.begin() + i, msg_str);
    aether->domain_->LoadRoot(clients[i]);
    assert(clients[i]);
    // Set adapter for all clouds in the client to work though.
    clients[i]->cloud()->set_adapter(adapter);
    auto receiver_connection = clients[i]->client_connection();
    receiver_connections.insert(receiver_connections.begin() + i,
                                receiver_connection);
    ae::Ptr<ae::P2pSafeStream> receiver_stream{};
    ae::Subscription receiver_subscription{};

    stream_subscriptions.insert(
        stream_subscriptions.begin() + i,
        receiver_connections[i]->new_stream_event().Subscribe(
            [&](auto uid, auto stream_id, auto const& message_stream) {
              AE_TELED_DEBUG("Received stream from {}", uid);
              receiver_stream = ae::MakePtr<ae::P2pSafeStream>(
                  *aether->action_processor, config,
                  ae::MakePtr<ae::P2pStream>(*aether->action_processor,
                                             clients[i], uid, stream_id,
                                             message_stream));
              receiver_streams.insert(receiver_streams.begin() + i,
                                      receiver_stream);

              receiver_subscriptions.insert(
                  receiver_subscriptions.begin() + i,
                  receiver_streams[i]->in().out_data_event().Subscribe(
                      [&](auto const& data) {
                        auto str_msg = std::string(
                            reinterpret_cast<const char*>(data.data()),
                            data.size());
                        AE_TELED_DEBUG("Received a message [{}]", str_msg);
                        auto msgit = std::find(std::begin(messages),
                                               std::end(messages), str_msg);
                        if (msgit != std::end(messages)) {
                          receive_count++;
                        }
                        auto confirm_msg = std::string{"confirmed "} + str_msg;
                        receiver_streams[i]->in().Write(
                            {confirm_msg.data(),
                             confirm_msg.data() + confirm_msg.size()},
                            ae::Now());
                      }));
            }));

    auto sender_stream = ae::MakePtr<ae::P2pSafeStream>(
        *aether->action_processor, config,
        ae::MakePtr<ae::P2pStream>(*aether->action_processor, clients[i],
                                   clients[i]->uid(), ae::StreamId{static_cast<std::uint8_t>(i)}));
    sender_streams.insert(sender_streams.begin() + i, sender_stream);

    AE_TELED_DEBUG("Send messages from sender {} to receiver {} message {}", i,
                   i, messages[i]);
    sender_streams[i]->in().Write(
        {std::begin(messages[i]), std::end(messages[i])},
                                ae::Now());

    sender_subscriptions.insert(
        sender_subscriptions.begin() + i,
        sender_streams[i]->in().out_data_event().Subscribe(
            [&](auto const& data) {
              auto str_response = std::string(
                  reinterpret_cast<const char*>(data.data()), data.size());
              AE_TELED_DEBUG("Received a response [{}], confirm_count {}",
                             str_response, confirm_count);
              confirm_count++;
            }));
  }

  while ((receive_count < messages.size()) ||
         (confirm_count < messages.size())) {
    AE_TELED_DEBUG("receive_count {} confirm_count {}", receive_count,
                   confirm_count);
    auto current_time = ae::Now();
    auto next_time = aether->domain_->Update(current_time);
    aether->action_processor->get_trigger().WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
#if ((defined(ESP_PLATFORM)))
    auto heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    AE_TELED_INFO("Heap size {}", heap_free);
#endif
  }

  aether->domain_->Update(ae::ClockType::now());

  // save objects state
  domain.SaveRoot(aether);

  return 0;
}
