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

#ifndef EXAMPLES_C_API_AETHER_CLASS_H_
#define EXAMPLES_C_API_AETHER_CLASS_H_

#include "aether_capi.h"
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

namespace ae::c_api_test {
enum class State : std::uint8_t {
  kRegistration,
  kConfigureReceiver,
  kConfigureSender,
  kSendMessages,
  kWaitDone,
  kResult,
  kError,
};

class CApiTestAction : public Action<CApiTestAction> {
 public:
  explicit CApiTestAction(Ptr<AetherApp> const& aether_app, void (*pt2Func)(CUid uid, uint8_t const* data, size_t size, void* user_data));
  TimePoint Update(TimePoint current_time) override;
  State GetState();
  void RegisterClients();
  void ConfigureReceiver(void (*pt2Func)(CUid uid, uint8_t const* data,
                                          size_t size, void* user_data));
  void ConfigureSender();
  void SendMessages(ae::TimePoint current_time, std::string msg);
  void CallFunc(CUid uid, uint8_t const* data, size_t size, void* user_data);

 private:
  Aether::ptr aether_;
  void (*Pt2Func_)(CUid uid, uint8_t const* data, size_t size, void* user_data);
  
  Client::ptr receiver_;
  std::unique_ptr<ae::ByteIStream> receiver_stream_;
  Client::ptr sender_;
  std::unique_ptr<ae::ByteIStream> sender_stream_;
  std::size_t clients_registered_;
  std::size_t receive_count_;
  std::size_t confirm_count_;
  TimePoint start_wait_time_;

  MultiSubscription registration_subscriptions_;
  Subscription receiver_new_stream_subscription_;
  Subscription receiver_message_subscription_;
  MultiSubscription response_subscriptions_;
  Subscription sender_message_subscription_;
  MultiSubscription send_subscriptions_;
  StateMachine<State> state_;
  Subscription state_changed_;
};

}  // namespace ae::c_api_test

#endif  // EXAMPLES_C_API_AETHER_CLASS_H_