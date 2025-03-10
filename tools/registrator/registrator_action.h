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

#ifndef TOOLS_REGISTRATOR_REGISTRATOR_ACTION_H_
#define TOOLS_REGISTRATOR_REGISTRATOR_ACTION_H_

#include "aether/memory.h"
#include "aether/aether_app.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#include "registrator/registrator_config.h"

namespace ae::registrator {
constexpr ae::SafeStreamConfig kSafeStreamConfig{
    std::numeric_limits<std::uint16_t>::max(),                // buffer_capacity
    (std::numeric_limits<std::uint16_t>::max() / 2) - 1,      // window_size
    (std::numeric_limits<std::uint16_t>::max() / 2) - 1 - 1,  // max_data_size
    4,                               // max_repeat_count
    std::chrono::milliseconds{200},  // wait_confirm_timeout
    {},                              // send_confirm_timeout
    std::chrono::milliseconds{200},  // send_repeat_timeout
};

class RegistratorAction : public Action<RegistratorAction> {
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
  explicit RegistratorAction(Ptr<AetherApp> const& aether_app,
                             RegistratorConfig const& registrator_config);
  TimePoint Update(TimePoint current_time) override;

 private:
  void RegisterClients();
  void ConfigureReceiver();
  void ConfigureSender();
  void SendMessages(TimePoint current_time);

  Aether::ptr aether_;
  RegistratorConfig registrator_config_;

  std::vector<std::string> messages_;
  std::vector<std::unique_ptr<P2pSafeStream>> sender_streams_;

  std::size_t clients_registered_{0};
  std::size_t receive_count_{0};
  std::size_t confirm_count_{0};

  MultiSubscription registration_subscriptions_;
  MultiSubscription receiver_message_subscriptions_;
  MultiSubscription response_subscriptions_;
  MultiSubscription sender_message_subscriptions_;
  MultiSubscription send_subscriptions_;
  StateMachine<State> state_;
  Subscription state_changed_;
};

}  // namespace ae::registrator

#endif  // TOOLS_REGISTRATOR_REGISTRATOR_ACTION_H_
