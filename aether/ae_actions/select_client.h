/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_AE_ACTIONS_SELECT_CLIENT_H_
#define AETHER_AE_ACTIONS_SELECT_CLIENT_H_

#include <cstdint>

#include "aether/config.h"
#include "aether/client.h"
#include "aether/ptr/ptr_view.h"
#include "aether/actions/action.h"
#include "aether/types/state_machine.h"
#include "aether/events/event_subscription.h"

namespace ae {
class Aether;
class Registration;

class SelectClientAction final : public Action<SelectClientAction> {
 public:
  enum class State : std::uint8_t {
    kClientReady,
    kWaitRegistration,
    kClientRegistered,
    kClientRegistrationError,
    kNoClientToSelect,
  };

  /**
   * \brief Create with client already ready.
   */
  SelectClientAction(ActionContext action_context, Client::ptr client);

#if AE_SUPPORT_REGISTRATION
  /**
   * \brief Wait for client registration or error.
   */
  SelectClientAction(ActionContext action_context, Aether& aether,
                     ActionPtr<Registration> registration,
                     std::string client_id);
#endif

  /**
   * \brief There is not client to select, throw error.
   */
  explicit SelectClientAction(ActionContext action_context);

  UpdateStatus Update();

  Client::ptr client() const;
  State state() const;

 private:
  Client::ptr client_;
  StateMachine<State> state_;

#if AE_SUPPORT_REGISTRATION
  std::string client_id_;
  ActionPtr<Registration> registration_;
  Subscription registration_sub_;
#endif
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_SELECT_CLIENT_H_
