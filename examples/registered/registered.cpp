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
#include "aether/memory.h"
#include "aether/state_machine.h"
#include "aether/actions/action.h"
#include "aether/transport/data_buffer.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"

#include "aether/ptr/ptr.h"
#include "aether/aether_app.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#include "aether/port/file_systems/static_domain_facility.h"
#include "aether/port/tele_init.h"

#include "aether/adapters/ethernet.h"
#include "aether/adapters/esp32_wifi.h"

#include "aether/tele/tele.h"

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
    kLoad,
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
        state_{State::kLoad},
        state_changed_{state_.changed_event().Subscribe(
            [this](auto) { Action::Trigger(); })} {
    AE_TELED_INFO("Registered test");
  }

  TimePoint Update(TimePoint current_time) override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kLoad:
          LoadClients();
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
      AE_TELED_DEBUG("Wait done receive_count {}", receive_count_);
      if (receive_count_ == messages_.size()) {
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
  void LoadClients() {
    AE_TELED_INFO("Testing loaded clients");
    for (std::size_t i{0}; i < aether_->clients().size(); i++) {
      auto msg_str = ae::Format("Test message for client {}", i);
      messages_.push_back(msg_str);
    }
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
    receive_count_ = 0;
    assert(aether_->clients().size() > 0);

    for (auto const& client : aether_->clients()) {
      auto& stream = sender_streams_.emplace_back(make_unique<P2pSafeStream>(
          *aether_->action_processor, kSafeStreamConfig,
          make_unique<P2pStream>(*aether_->action_processor, client,
                                 client->uid())));
      sender_message_subscriptions_.Push(
          stream->out_data_event().Subscribe([&](auto const& data) {
            auto str_response = std::string(
                reinterpret_cast<const char*>(data.data()), data.size());
            AE_TELED_DEBUG("Received a response [{}], confirm_count {}",
                           str_response, receive_count_);
            receive_count_++;
          }));
    }

    state_ = State::kSendMessages;
  }

  /**
   * \brief Send all messages at once.
   */
  void SendMessages(TimePoint /* current_time */) {
    std::uint8_t messages_cnt{0};

    AE_TELED_INFO("Send messages");

    for (auto const& sender_stream : sender_streams_) {
      auto msg = messages_[messages_cnt++];
      AE_TELED_DEBUG("Sending message {}", msg);
      auto send_action =
          sender_stream->Write(DataBuffer{std::begin(msg), std::end(msg)});
      send_subscriptions_.Push(
          send_action->ErrorEvent().Subscribe([&](auto const&) {
            AE_TELED_ERROR("Send message failed");
            state_ = State::kError;
          }));
    }

    state_ = State::kWaitDone;
  }

  Aether::ptr aether_;

  std::vector<std::unique_ptr<ae::P2pSafeStream>> sender_streams_;

  std::size_t clients_registered_;
  std::size_t receive_count_{0};

  MultiSubscription registration_subscriptions_;
  MultiSubscription receiver_message_subscriptions_;
  MultiSubscription response_subscriptions_;
  MultiSubscription sender_message_subscriptions_;
  MultiSubscription send_subscriptions_;
  StateMachine<State> state_;
  std::vector<std::string> messages_;
  Subscription state_changed_;
};

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
  auto aether_app = ae::AetherApp::Construct(ae::AetherAppConstructor{
#if defined STATIC_DOMAIN_FACILITY_ENABLED
      []() { return ae::make_unique<ae::StaticDomainFacility>(); }
#endif
  });

  auto registered_action = ae::registered::RegisteredAction{aether_app};

  auto success = registered_action.ResultEvent().Subscribe(
      [&](auto const&) { aether_app->Exit(0); });
  auto failed = registered_action.ErrorEvent().Subscribe(
      [&](auto const&) { aether_app->Exit(1); });

  while (!aether_app->IsExited()) {
    auto current_time = ae::Now();
    auto next_time = aether_app->Update(current_time);
    aether_app->WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
  }

  return aether_app->ExitCode();
}
