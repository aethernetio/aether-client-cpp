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

/**
 * \brief This is the main action to perform all busyness logic here.
 */
class CloudTestAction : public Action<CloudTestAction> {
  enum class State : std::uint8_t {
    kSendMessages,
    kWaitDone,
    kResult,
    kError,
  };

 public:
  explicit CloudTestAction(ActionContext action_context, Client::ptr sender,
                           Client::ptr receiver)
      : Action{action_context},
        action_context_{action_context},
        receiver_{std::move(receiver)},
        sender_{std::move(sender)},
        receive_count_{},
        confirm_count_{},
        state_{State::kSendMessages},
        messages_{
            "Hello, it's me",
            "I was wondering if, after all these years, you'd like to meet",
            "To go over everything",
            "They say that time's supposed to heal ya",
            "But I ain't done much healin'",
            "Hello, can you hear me?",
            "I'm in California dreaming about who we used to be",
            "When we were younger and free",
            "I've forgotten how it felt before the world fell at our feet"},
        state_changed_{state_.changed_event().Subscribe(
            [this](auto) { Action::Trigger(); })} {
    AE_TELED_INFO("Cloud test");

    /**
     * Make required preparation for receiving messages.
     * Subscribe to opening new stream event.
     * Subscribe to receiving message event.
     * Send confirmation to received message.
     */
    receive_new_stream_sub_ =
        receiver_->client_connection()->new_stream_event().Subscribe(
            [&](auto uid, auto raw_stream) {
              receiver_stream_ = make_unique<P2pSafeStream>(
                  action_context_, kSafeStreamConfig,
                  make_unique<P2pStream>(action_context_, receiver_, uid,
                                         std::move(raw_stream)));

              receive_message_sub_ =
                  receiver_stream_->out_data_event().Subscribe(
                      [&](auto const& data) {
                        auto str_msg = std::string(
                            reinterpret_cast<const char*>(data.data()),
                            data.size());
                        AE_TELED_DEBUG("Received a message [{}]", str_msg);
                        receive_count_++;
                        auto confirm_msg = std::string{"confirmed "} + str_msg;
                        auto response_action = receiver_stream_->Write(
                            {confirm_msg.data(),
                             confirm_msg.data() + confirm_msg.size()});
                        response_subs_.Push(
                            response_action->ErrorEvent().Subscribe(
                                [&](auto const&) {
                                  AE_TELED_ERROR("Send response failed");
                                  state_ = State::kError;
                                }));
                      });
            });
    /**
     * Make required preparation to send messages.
     * Create a sender to receiver stream.
     * Subscribe to receiving message event for confirmations \see
     * ConfigureReceiver.
     */
    sender_stream_ = make_unique<P2pSafeStream>(
        action_context_, kSafeStreamConfig,
        make_unique<P2pStream>(action_context_, sender_, receiver_->uid()));
    sender_message_sub_ =
        sender_stream_->out_data_event().Subscribe([&](auto const& data) {
          auto str_response = std::string(
              reinterpret_cast<const char*>(data.data()), data.size());
          AE_TELED_DEBUG("Received a response [{}], confirm_count {}",
                         str_response, confirm_count_);
          Action::Trigger();
          confirm_count_++;
        });
  }

  ActionResult Update() {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kSendMessages:
          SendMessages();
          break;
        case State::kWaitDone:
          break;
        case State::kResult:
          return ActionResult::Result();
        case State::kError:
          return ActionResult::Error();
      }
    }
    // wait till all sent messages received and confirmed
    if (state_.get() == State::kWaitDone) {
      AE_TELED_DEBUG("Wait done receive_count {}, confirm_count {}",
                     receive_count_, confirm_count_);
      auto current_time = Now();
      if ((receive_count_ == messages_.size()) &&
          (confirm_count_ == messages_.size())) {
        state_ = State::kResult;
      } else {
        if ((start_wait_time_ + std::chrono::seconds{10}) > current_time) {
          return ActionResult::Delay(start_wait_time_ +
                                     std::chrono::seconds{10});
        }
        state_ = State::kError;
      }
    }
    return {};
  }

 private:
  /**
   * \brief Send all messages at once.
   */
  void SendMessages() {
    AE_TELED_INFO("Send messages");
    for (auto const& msg : messages_) {
      auto send_action =
          sender_stream_->Write(DataBuffer{std::begin(msg), std::end(msg)});
      send_subs_.Push(send_action->ErrorEvent().Subscribe([&](auto const&) {
        AE_TELED_ERROR("Send message failed");
        state_ = State::kError;
      }));
    }
    start_wait_time_ = Now();
    state_ = State::kWaitDone;
  }

  ActionContext action_context_;

  Client::ptr receiver_;
  std::unique_ptr<ByteIStream> receiver_stream_;
  Client::ptr sender_;
  std::unique_ptr<ByteIStream> sender_stream_;
  std::size_t receive_count_;
  std::size_t confirm_count_;
  TimePoint start_wait_time_;

  Subscription receive_new_stream_sub_;
  Subscription receive_message_sub_;
  MultiSubscription response_subs_;
  Subscription sender_message_sub_;
  MultiSubscription send_subs_;
  StateMachine<State> state_;
  std::vector<std::string> messages_;
  Subscription state_changed_;
};

}  // namespace ae::cloud_test

int AetherCloudExample() {
  ae::SerialInit serial_init = {"COM17", 115200};

  ae::ModemInit modem_init{
    serial_init,                  // Serial port
    {1,1,1,1},                    // Pin code
    false,                        // Use pin
    ae::kModemMode::kModeAuto,    // Modem mode
    "25020",                      // Operator code
    "TELE2 internet",             // Operator name
    "internet.tele2.ru",          // APN
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
   * To configure its creation \see AetherAppConstructor.
   */
   auto aether_app = ae::AetherApp::Construct(
      ae::AetherAppConstructor{}
#if defined AE_DISTILLATION
          .Adapter([modem_init](
                       ae::Domain* domain,
                      ae::Aether::ptr const& aether) -> ae::Adapter::ptr {
#  if defined ESP32_WIFI_ADAPTER_ENABLED
            auto adapter = domain->CreateObj<ae::Esp32WifiAdapter>(
                ae::GlobalId::kEsp32WiFiAdapter, aether, aether->poller,
                std::string(kWifiSsid), std::string(kWifiPass));
#  elif defined MODEM_ADAPTER_ENABLED
            auto adapter = domain->CreateObj<ae::ModemAdapter>(
                ae::GlobalId::kModemAdapter, aether, aether->poller,
               modem_init);
#  else
            auto adapter = domain->CreateObj<ae::EthernetAdapter>(
                ae::GlobalId::kEthernetAdapter, aether, aether->poller);
#  endif
            return adapter;
          })
#endif
  );

  /**
   * Start clients selection or registration.
   * Clients might be loaded from data storage saved during previous run.
   * Or Registered if not found.
   */
  auto select_client_a = aether_app->aether()->SelectClient(
      ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"), 1);
  auto select_client_b = aether_app->aether()->SelectClient(
      ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"), 2);

  auto clients_selected = ae::CumulativeEvent{
      [](auto& action) { return action.client(); },
      select_client_a->ResultEvent(), select_client_b->ResultEvent()};

  auto clients_failed = ae::CumulativeEvent{select_client_a->ErrorEvent(),
                                            select_client_b->ErrorEvent()};
  clients_failed.Subscribe([&]() { aether_app->Exit(1); });

  /**
   * Make cloud test action after all clients are selected (loaded or
   * registered). Test is an asynchronous action. Send and receive a bunch of
   * messages and wait confirmations.
   */
  std::unique_ptr<ae::cloud_test::CloudTestAction> cloud_test_action;
  clients_selected.Subscribe([&](auto& event) {
    cloud_test_action = ae::make_unique<ae::cloud_test::CloudTestAction>(
        *aether_app, event[0], event[1]);

    cloud_test_action->ResultEvent().Subscribe(
        [&](auto const&) { aether_app->Exit(0); });
    cloud_test_action->ErrorEvent().Subscribe(
        [&](auto const&) { aether_app->Exit(1); });
  });

  /**
   * Application loop.
   * All the asynchronous actions are updated on this loop.
   * WaitUntil either waits until the next selected time or some action triggers
   * new event.
   */
  while (!aether_app->IsExited()) {
    auto current_time = ae::Now();
    auto next_time = aether_app->Update(current_time);
    aether_app->WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
  }

  return aether_app->ExitCode();
}
