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

#ifndef EXAMPLES_C_API_AETHER_FUNCTIONS_H_
#define EXAMPLES_C_API_AETHER_FUNCTIONS_H_

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

enum class State : std::uint8_t {
  kRegistration,
  kConfigureReceiver,
  kConfigureSender,
  kSendMessages,
  kWaitDone,
  kResult,
  kError,
};

void SetAether(ae::Ptr<ae::AetherApp> const& aether_app);
State RegisterClients();
State ConfigureReceiver(void (*pt2Func)(CUid uid, uint8_t const* data,
                                        size_t size, void* user_data));
State ConfigureSender();
State SendMessages(ae::TimePoint current_time, std::string msg);

#endif  // EXAMPLES_C_API_AETHER_FUNCTIONS_H_