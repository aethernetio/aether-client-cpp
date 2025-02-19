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

static constexpr std::string_view kWifiSsid = "Test123";
static constexpr std::string_view kWifiPass = "Test123";

namespace ae::cloud_test {
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
class CloudTestAction : public Action<CloudTestAction> {
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
  explicit CloudTestAction(Ptr<AetherApp> const& aether_app)
      : Action{*aether_app},
        aether_{aether_app->aether()},
        state_{State::kRegistration},
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
  void RegisterClients() {
#if AE_SUPPORT_REGISTRATION
    {
      AE_TELED_INFO("Client registration");
      // register receiver and sender
      clients_registered_ = aether_->clients().size();

      for (auto i = clients_registered_; i < 2; i++) {
        auto reg_action = aether_->RegisterClient(
            Uid{MakeLiteralArray("3ac931653d37497087a6fa4ee27744e4")});
        registration_subscriptions_.Push(
            reg_action->SubscribeOnResult([&](auto const&) {
              ++clients_registered_;
              if (clients_registered_ == 2) {
                state_ = State::kConfigureReceiver;
              }
            }),
            reg_action->SubscribeOnError([&](auto const&) {
              AE_TELED_ERROR("Registration error");
              state_ = State::kError;
            }));
      }
      // all required clients already registered
      if (clients_registered_ == 2) {
        state_ = State::kConfigureReceiver;
        return;
      }
    }
#else
    // skip registration
    state_ = State::kConfigureReceiver;
#endif
  }

  /**
   * \brief Make required preparation for receiving messages.
   * Subscribe to opening new stream event.
   * Subscribe to receiving message event.
   * Send confirmation to received message.
   */
  void ConfigureReceiver() {
    AE_TELED_INFO("Receiver configuration");
    receive_count_ = 0;
    assert(!aether_->clients().empty());
    receiver_ = aether_->clients()[0];
    auto receiver_connection = receiver_->client_connection();
    receiver_new_stream_subscription_ =
        receiver_connection->new_stream_event().Subscribe(
            [&](auto uid, auto stream_id, auto raw_stream) {
              receiver_stream_ = MakePtr<P2pSafeStream>(
                  *aether_->action_processor, kSafeStreamConfig,
                  MakePtr<P2pStream>(*aether_->action_processor, receiver_, uid,
                                     stream_id, std::move(raw_stream)));
              receiver_message_subscription_ =
                  receiver_stream_->in().out_data_event().Subscribe(
                      [&](auto const& data) {
                        auto str_msg = std::string(
                            reinterpret_cast<const char*>(data.data()),
                            data.size());
                        AE_TELED_DEBUG("Received a message [{}]", str_msg);
                        receive_count_++;
                        auto confirm_msg = std::string{"confirmed "} + str_msg;
                        auto response_action = receiver_stream_->in().Write(
                            {confirm_msg.data(),
                             confirm_msg.data() + confirm_msg.size()},
                            ae::Now());
                        response_subscriptions_.Push(
                            response_action->SubscribeOnError([&](auto const&) {
                              AE_TELED_ERROR("Send response failed");
                              state_ = State::kError;
                            }));
                      });
            });
    state_ = State::kConfigureSender;
  }

  /**
   * \brief Make required preparation to send messages.
   * Create a sender to receiver stream.
   * Subscribe to receiving message event for confirmations \see
   * ConfigureReceiver.
   */
  void ConfigureSender() {
    AE_TELED_INFO("Sender configuration");
    confirm_count_ = 0;
    assert(aether_->clients().size() > 1);
    sender_ = aether_->clients()[1];
    sender_stream_ = MakePtr<P2pSafeStream>(
        *aether_->action_processor, kSafeStreamConfig,
        MakePtr<P2pStream>(*aether_->action_processor, sender_,
                           receiver_->uid(), StreamId{0}));
    sender_message_subscription_ =
        sender_stream_->in().out_data_event().Subscribe([&](auto const& data) {
          auto str_response = std::string(
              reinterpret_cast<const char*>(data.data()), data.size());
          AE_TELED_DEBUG("Received a response [{}], confirm_count {}",
                         str_response, confirm_count_);
          confirm_count_++;
        });
    state_ = State::kSendMessages;
  }

  /**
   * \brief Send all messages at once.
   */
  void SendMessages(TimePoint current_time) {
    AE_TELED_INFO("Send messages");
    for (auto const& msg : messages_) {
      auto send_action = sender_stream_->in().Write(
          DataBuffer{std::begin(msg), std::end(msg)}, current_time);
      send_subscriptions_.Push(send_action->SubscribeOnError([&](auto const&) {
        AE_TELED_ERROR("Send message failed");
        state_ = State::kError;
      }));
    }
    state_ = State::kWaitDone;
  }

  Aether::ptr aether_;

  Client::ptr receiver_;
  Ptr<ByteStream> receiver_stream_;
  Client::ptr sender_;
  Ptr<ByteStream> sender_stream_;
  std::size_t clients_registered_;
  std::size_t receive_count_;
  std::size_t confirm_count_;

  MultiSubscription registration_subscriptions_;
  Subscription receiver_new_stream_subscription_;
  Subscription receiver_message_subscription_;
  MultiSubscription response_subscriptions_;
  Subscription sender_message_subscription_;
  MultiSubscription send_subscriptions_;
  StateMachine<State> state_;
  std::vector<std::string> messages_;
  Subscription state_changed_;
};

}  // namespace ae::cloud_test

int AetherCloudExample();

int AetherCloudExample() {
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
            auto adapter = domain->CreateObj<ae::Esp32WifiAdapter>(
                ae::GlobalId::kEsp32WiFiAdapter, aether, aether->poller,
                std::string(WIFI_SSID), std::string(WIFI_PASS));
#  else
            auto adapter = domain->CreateObj<ae::EthernetAdapter>(
                ae::GlobalId::kEthernetAdapter, aether, aether->poller);
#  endif
            return adapter;
          })
#  if AE_SUPPORT_REGISTRATION
          .RegCloud([](ae::Ptr<ae::Domain> const& domain,
                       ae::Aether::ptr const& /* aether */) {
            auto registration_cloud = domain->CreateObj<ae::RegistrationCloud>(
                ae::kRegistrationCloud);
            // localhost
            registration_cloud->AddServerSettings(ae::IpAddressPortProtocol{
                {ae::IpAddress{ae::IpAddress::Version::kIpV4, {127, 0, 0, 1}},
                 9010},
                ae::Protocol::kTcp});
            // cloud address
            registration_cloud->AddServerSettings(ae::IpAddressPortProtocol{
                {ae::IpAddress{ae::IpAddress::Version::kIpV4,
                               {34, 60, 244, 148}},
                 9010},
                ae::Protocol::kTcp});
            // cloud name address
            registration_cloud->AddServerSettings(ae::NameAddress{
                "registration.aethernet.io", 9010, ae::Protocol::kTcp});
            return registration_cloud;
          })
#  endif  // AE_SUPPORT_REGISTRATION
#endif
  );

  auto cloud_test_action = ae::cloud_test::CloudTestAction{aether_app};

  auto success = cloud_test_action.SubscribeOnResult(
      [&](auto const&) { aether_app->Exit(0); });
  auto failed = cloud_test_action.SubscribeOnError(
      [&](auto const&) { aether_app->Exit(1); });

  while (!aether_app->IsExited()) {
    auto current_time = ae::Now();
    auto next_time = aether_app->Update(current_time);
    aether_app->WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
  }

  return aether_app->ExitCode();
}
