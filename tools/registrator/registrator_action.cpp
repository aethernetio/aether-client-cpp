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

#include "registrator/registrator_action.h"

#include "aether/aether.h"
#include "aether/common.h"
#include "aether/literal_array.h"

constexpr bool kUseSelfTest = false;

namespace ae::registrator {

RegistratorAction::RegistratorAction(
    Ptr<AetherApp> const& aether_app,
    RegistratorConfig const& registrator_config)
    : Action{*aether_app},
      aether_{aether_app->aether()},
      registrator_config_{registrator_config},
      messages_{},
      state_{State::kRegistration},
      state_changed_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELED_INFO("Registration test");
}

TimePoint RegistratorAction::Update(TimePoint current_time) {
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
    if (kUseSelfTest) {
      AE_TELED_DEBUG("Wait done receive_count {}, confirm_count {}",
                     receive_count_, confirm_count_);
      if ((receive_count_ == messages_.size()) &&
          (confirm_count_ == messages_.size())) {
        state_ = State::kResult;
      }
    } else {
      AE_TELED_DEBUG("Wait done clients_registered_ {}", clients_registered_);
      if (clients_registered_ == registrator_config_.GetClientsTotal()) {
        state_ = State::kResult;
      }
    }
    // if no any events happens wake up after 1 second
    return current_time + std::chrono::seconds{1};
  }
  return current_time;
}

/**
 * \brief Perform a client registration.
 * We need a two clients for this test.
 */
void RegistratorAction::RegisterClients() {
  {
    std::uint16_t messages_cnt{0};

    AE_TELED_INFO("Client registration");
#if AE_SUPPORT_REGISTRATION
    for (auto p : registrator_config_.GetParents()) {
      auto parent_uid = ae::Uid::FromString(p.uid_str);

      auto clients_num = p.clients_num;

      for (std::uint8_t i{0}; i < clients_num; i++) {
        auto reg_action = aether_->RegisterClient(parent_uid);

        registration_subscriptions_.Push(
            reg_action->ResultEvent().Subscribe([&](auto const&) {
              ++clients_registered_;
              if (clients_registered_ ==
                  registrator_config_.GetClientsTotal()) {
                state_ = State::kConfigureReceiver;
              }
            }),
            reg_action->ErrorEvent().Subscribe([&](auto const&) {
              AE_TELED_ERROR("Registration error");
              state_ = State::kError;
            }));

        auto msg = std::string("Message to client number " +
                               std::to_string(messages_cnt++));
        messages_.push_back(msg);
      }
    }
  }
#else
  }
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
void RegistratorAction::ConfigureReceiver() {
  // We don't need to set up the receiver.

  state_ = State::kConfigureSender;
}

/**
 * \brief Make required preparation to send messages.
 * Create a sender to receiver stream.
 * Subscribe to receiving message event for confirmations \see
 * ConfigureReceiver.
 */
void RegistratorAction::ConfigureSender() {
  std::uint8_t clients_cnt{0};

  if (kUseSelfTest) {
    AE_TELED_INFO("Sender configuration");
    confirm_count_ = 0;
    assert(aether_->clients().size() == registrator_config_.GetClientsTotal());

    for (auto const& client : aether_->clients()) {
      auto sender_stream = make_unique<P2pSafeStream>(
          *aether_->action_processor, kSafeStreamConfig,
          make_unique<P2pStream>(*aether_->action_processor, client,
                                 client->uid()));
      sender_streams_.emplace_back(std::move(sender_stream));

      sender_message_subscriptions_.Push(
          sender_streams_[clients_cnt]->out_data_event().Subscribe(
              [&](auto const& data) {
                auto str_response = std::string(
                    reinterpret_cast<const char*>(data.data()), data.size());
                AE_TELED_DEBUG("Received a response [{}], confirm_count {}",
                               str_response, confirm_count_);
                confirm_count_++;
              }));

      receiver_message_subscriptions_.Push(
          sender_streams_[clients_cnt]->out_data_event().Subscribe(
              [&](auto const& data) {
                auto str_msg = std::string(
                    reinterpret_cast<const char*>(data.data()), data.size());
                AE_TELED_DEBUG("Received a message [{}]", str_msg);
                auto confirm_msg = std::string{"confirmed "} + str_msg;
                auto response_action = sender_streams_[receive_count_++]->Write(
                    {confirm_msg.data(),
                     confirm_msg.data() + confirm_msg.size()});
                response_subscriptions_.Push(
                    response_action->ErrorEvent().Subscribe([&](auto const&) {
                      AE_TELED_ERROR("Send response failed");
                      state_ = State::kError;
                    }));
              }));

      clients_cnt++;
    }
  }

  state_ = State::kSendMessages;
}

/**
 * \brief Send all messages at once.
 */
void RegistratorAction::SendMessages(TimePoint current_time) {
  std::uint8_t messages_cnt{0};

  if (kUseSelfTest) {
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
  }

  state_ = State::kWaitDone;
}

}  // namespace ae::registrator
