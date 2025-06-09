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

#include "aether_functions.h"

ae::Aether::ptr aether_;

ae::Client::ptr receiver_;
std::unique_ptr<ae::ByteIStream> receiver_stream_;
ae::Client::ptr sender_;
std::unique_ptr<ae::ByteIStream> sender_stream_;
std::size_t clients_registered_;
std::size_t receive_count_;
std::size_t confirm_count_;
ae::TimePoint start_wait_time_;

ae::MultiSubscription registration_subscriptions_;
ae::Subscription receiver_new_stream_subscription_;
ae::Subscription receiver_message_subscription_;
ae::MultiSubscription response_subscriptions_;
ae::Subscription sender_message_subscription_;
ae::MultiSubscription send_subscriptions_;

constexpr ae::SafeStreamConfig kSafeStreamConfig{
    std::numeric_limits<std::uint16_t>::max(),                // buffer_capacity
    (std::numeric_limits<std::uint16_t>::max() / 2) - 1,      // window_size
    (std::numeric_limits<std::uint16_t>::max() / 2) - 1 - 1,  // max_data_size
    10,                              // max_repeat_count
    std::chrono::milliseconds{600},  // wait_confirm_timeout
    {},                              // send_confirm_timeout
    std::chrono::milliseconds{400},  // send_repeat_timeout
};

void SetAether(ae::Ptr<ae::AetherApp> const& aether_app){
  aether_ = aether_app->aether();
}

/**
 * \brief Perform a client registration.
 * We need a two clients for this test.
 */
State RegisterClients() {
  State state_;

#if AE_SUPPORT_REGISTRATION
  {
    AE_TELED_INFO("Client registration");
    // register receiver and sender
    clients_registered_ = aether_->clients().size();

    for (auto i = clients_registered_; i < 2; i++) {
      auto reg_action = aether_->RegisterClient(
          ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"));
      registration_subscriptions_.Push(
          reg_action->ResultEvent().Subscribe([&](auto const&) {
            ++clients_registered_;
            if (clients_registered_ == 2) {
              state_ = State::kConfigureReceiver;
            }
          }),
          reg_action->ErrorEvent().Subscribe([&](auto const&) {
            AE_TELED_ERROR("Registration error");
            state_ = State::kError;
          }));
    }
    // all required clients already registered
    if (clients_registered_ == 2) {
      state_ = State::kConfigureReceiver;
      return state_;
    }
  }
#else
  // skip registration
  state_ = State::kConfigureReceiver;
#endif

  return state_;
}

/**
 * \brief Make required preparation for receiving messages.
 * Subscribe to opening new stream event.
 * Subscribe to receiving message event.
 * Send confirmation to received message.
 */
State ConfigureReceiver(void (*pt2Func)(CUid uid, uint8_t const* data,
                                        size_t size, void* user_data)) {
  State state_;
  CUid cuid;
  
  AE_TELED_INFO("Receiver configuration");
  receive_count_ = 0;
  assert(!aether_->clients().empty());
  receiver_ = aether_->clients()[0];
  auto receiver_connection = receiver_->client_connection();
  receiver_new_stream_subscription_ =
      receiver_connection->new_stream_event().Subscribe([&](auto uid,
                                                            auto raw_stream) {
        receiver_stream_ = make_unique<ae::P2pSafeStream>(
            *aether_->action_processor, kSafeStreamConfig,
            make_unique<ae::P2pStream>(*aether_->action_processor, receiver_,
                                       uid, std::move(raw_stream)));
        receiver_message_subscription_ =
            receiver_stream_->out_data_event().Subscribe([&](auto const& data) {
              auto str_msg = std::string(
                  reinterpret_cast<const char*>(data.data()), data.size());
              AE_TELED_DEBUG("Received a message [{}]", str_msg);
              for(uint8_t i=0; i<16; i++) {
                cuid.id[i] = uid.value.data()[i];
              }
              pt2Func(cuid, data.data(), data.size(), nullptr);
              receive_count_++;
              auto confirm_msg = std::string{"confirmed "} + str_msg;
              auto response_action = receiver_stream_->Write(
                  {confirm_msg.data(),
                   confirm_msg.data() + confirm_msg.size()});
              response_subscriptions_.Push(
                  response_action->ErrorEvent().Subscribe([&](auto const&) {
                    AE_TELED_ERROR("Send response failed");
                    state_ = State::kError;
                  }));
            });
      });
  state_ = State::kConfigureSender;

  return state_;
}

/**
 * \brief Make required preparation to send messages.
 * Create a sender to receiver stream.
 * Subscribe to receiving message event for confirmations \see
 * ConfigureReceiver.
 */
State ConfigureSender() {
  State state_;

  AE_TELED_INFO("Sender configuration");
  confirm_count_ = 0;
  assert(aether_->clients().size() > 1);
  sender_ = aether_->clients()[1];
  sender_stream_ = make_unique<ae::P2pSafeStream>(
      *aether_->action_processor, kSafeStreamConfig,
      make_unique<ae::P2pStream>(*aether_->action_processor, sender_,
                                 receiver_->uid()));
  sender_message_subscription_ =
      sender_stream_->out_data_event().Subscribe([&](auto const& data) {
        auto str_response = std::string(
            reinterpret_cast<const char*>(data.data()), data.size());
        AE_TELED_DEBUG("Received a response [{}], confirm_count {}",
                       str_response, confirm_count_);
        aether_->action_processor->get_trigger().Trigger();
        confirm_count_++;
      });
  state_ = State::kSendMessages;

  return state_;
}

/**
 * \brief Send all messages at once.
 */
State SendMessages(ae::TimePoint current_time, std::string msg) {
  State state_;

  AE_TELED_INFO("Send messages");

  auto send_action =
      sender_stream_->Write(ae::DataBuffer{std::begin(msg), std::end(msg)});
  send_subscriptions_.Push(
      send_action->ErrorEvent().Subscribe([&](auto const&) {
        AE_TELED_ERROR("Send message failed");
        state_ = State::kError;
      }));

  start_wait_time_ = current_time;
  state_ = State::kWaitDone;

  return state_;
}
