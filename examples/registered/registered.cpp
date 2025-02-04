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
#include <chrono>
#include <cstdint>

#include "aether/common.h"
#include "aether/state_machine.h"
#include "aether/literal_array.h"
#include "aether/actions/action.h"
#include "aether/stream_api/istream.h"
#include "aether/transport/data_buffer.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"

#include "aether/global_ids.h"
#include "aether/aether_app.h"
#include "aether/ae_actions/registration/registration.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#include "aether/port/tele_init.h"

#include "aether/adapters/ethernet.h"
#include "aether/adapters/esp32_wifi.h"

#include "aether/tele/tele.h"
#include "aether/tele/ios_time.h"

static constexpr int wait_time = 100;

namespace ae::registered {

constexpr ae::SafeStreamConfig kSafeStreamConfig{
    std::numeric_limits<std::uint16_t>::max(),                // buffer_capacity
    (std::numeric_limits<std::uint16_t>::max() / 2) - 1,      // window_size
    (std::numeric_limits<std::uint16_t>::max() / 2) - 1 - 1,  // max_data_size
    4,                               // max_repeat_count
    std::chrono::milliseconds{200},  // wait_confirm_timeout
    {},                              // send_confirm_timeout
    std::chrono::milliseconds{200},  // send_repeat_timeout
};

/**
 * \brief This is the main action to perform all busyness logic here.
 */
class RegisteredAction : public Action<RegisteredAction> {
  enum class State : std::uint8_t {
    kRegistration,
    kConfigureReceiver,
    kConfigureSender,
    kSendMessages,
    kWaitDone,
    kResult,
    kError,
  };

 public:
  explicit RegisteredAction(Ptr<AetherApp> const& aether_app)
      : Action{*aether_app},
        aether_{aether_app->aether()},
        state_{State::kRegistration},
        messages_{},
        state_changed_{state_.changed_event().Subscribe(
            [this](auto) { Action::Trigger(); })} {
    AE_TELED_INFO("Registered test");
  }

  TimePoint Update(TimePoint current_time) override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kRegistration:
          RegisterClients();
          break;
        case State::kConfigureReceiver:
          ConfigureReceiver();
          break;
        case State::kConfigureSender:
          ConfigureSender();
          break;
        case State::kSendMessages:
          SendMessages(current_time);
          break;
        case State::kWaitDone:
          break;
        case State::kResult:
          Action::Result(*this);
          return current_time;
        case State::kError:
          Action::Error(*this);
          return current_time;
      }
    }
    // wait till all sent messages received and confirmed
    if (state_.get() == State::kWaitDone) {
      AE_TELED_DEBUG("Wait done receive_count {}, confirm_count {}",
                     receive_count_, confirm_count_);
      if ((receive_count_ == messages_.size()) &&
          (confirm_count_ == messages_.size())) {
        state_ = State::kResult;
      }
      // if no any events happens wake up after 1 second
      return current_time + std::chrono::seconds{1};
    }
    return current_time;
  }

 private:
  /**
   * \brief Perform a client registration.
   * We need a two clients for this test.
   */
  void RegisterClients() { AE_TELED_INFO("Test of registered clients"); }

  /**
   * \brief Make required preparation for receiving messages.
   * Subscribe to opening new stream event.
   * Subscribe to receiving message event.
   * Send confirmation to received message.
   */
  void ConfigureReceiver() { AE_TELED_INFO("Receiver configuration"); }

  /**
   * \brief Make required preparation to send messages.
   * Create a sender to receiver stream.
   * Subscribe to receiving message event for confirmations \see
   * ConfigureReceiver.
   */
  void ConfigureSender() { AE_TELED_INFO("Sender configuration"); }

  /**
   * \brief Send all messages at once.
   */
  void SendMessages(TimePoint current_time) { AE_TELED_INFO("Send messages"); }

  Aether::ptr aether_;

  std::vector<std::string> messages_;
  Client::ptr receiver_;
  Ptr<ByteStream> receiver_stream_;
  Client::ptr sender_;
  Ptr<ByteStream> sender_stream_;
  std::size_t clients_registered_;
  std::size_t receive_count_{0};
  std::size_t confirm_count_{0};

  MultiSubscription registration_subscriptions_;
  Subscription receiver_new_stream_subscription_;
  Subscription receiver_message_subscription_;
  MultiSubscription response_subscriptions_;
  Subscription sender_message_subscription_;
  MultiSubscription send_subscriptions_;
  StateMachine<State> state_;
  Subscription state_changed_;
};

//************************************************************************************************************************************
/*static Aether::ptr LoadAether(Domain& domain) {
  Aether::ptr aether;
  aether.SetId(GlobalId::kAether);
  domain.LoadRoot(aether);
  assert(aether);
  return aether;
}*/

}  // namespace ae::registered

int AetherRegistered();

int AetherRegistered() {
  /**
   * Construct a main aether application class.
   * It's include a Domain and Aether instances accessible by getter methods.
   * It has Update, WaitUntil, Exit, IsExit, ExitCode methods to integrate it in
   * your update loop.
   * Also it has action context protocol implementation \see Action.
   * To configure its creation \see AetherAppConstructor.
   */
  auto aether_app = ae::AetherApp::Construct(
      ae::AetherAppConstructor{}
#if defined AE_DISTILLATION
          .Adapter([](ae::Ptr<ae::Domain> const& domain,
                      ae::Aether::ptr const& aether) -> ae::Adapter::ptr {
#  if defined ESP32_WIFI_ADAPTER_ENABLED
            auto adapter = domain.CreateObj<ae::Esp32WifiAdapter>(
                ae::GlobalId::kEsp32WiFiAdapter, aether, aether->poller,
                std::string(WIFI_SSID), std::string(WIFI_PASS));
#  else
            auto adapter = domain->CreateObj<ae::EthernetAdapter>(
                ae::GlobalId::kEthernetAdapter, aether, aether->poller);
#  endif
            return adapter;
          })
#endif
  );

  auto registered_action = ae::registered::RegisteredAction{aether_app};

  auto success = registered_action.SubscribeOnResult(
      [&](auto const&) { aether_app->Exit(0); });
  auto failed = registered_action.SubscribeOnError(
      [&](auto const&) { aether_app->Exit(1); });

  while (!aether_app->IsExited()) {
    auto current_time = ae::Now();
    auto next_time = aether_app->Update(current_time);
    aether_app->WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
  }

  return aether_app->ExitCode();

//************************************************************************************************************************************
/*  ae::TeleInit::Init();
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

  std::vector<ae::Client::ptr> clients{};
  std::vector<std::string> messages{};

  std::vector<ae::Ptr<ae::P2pSafeStream>> client_stream_to_self{};
  ae::MultiSubscription sender_subscriptions{};

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

    client_stream_to_self.insert(
        client_stream_to_self.begin() + i,
        ae::MakePtr<ae::P2pSafeStream>(
            *aether->action_processor, config,
            ae::MakePtr<ae::P2pStream>(
                *aether->action_processor, clients[i], clients[i]->uid(),
                ae::StreamId{static_cast<std::uint8_t>(i)})));

    AE_TELED_DEBUG("Send messages from sender {} to receiver {} message {}", i,
                   i, messages[i]);
    client_stream_to_self[i]->in().Write(
        {std::begin(messages[i]), std::end(messages[i])}, ae::Now());

    sender_subscriptions.Push(
        client_stream_to_self[i]->in().out_data_event().Subscribe(
            [&](auto const& data) {
              auto str_response = std::string(
                  reinterpret_cast<const char*>(data.data()), data.size());
              AE_TELED_DEBUG("Received a response [{}], receive_count {}",
                             str_response, receive_count);
              receive_count++;
            }));
  }

  while (receive_count < messages.size()) {
    AE_TELED_DEBUG("receive_count {}", receive_count);
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

  return 0;*/
}
