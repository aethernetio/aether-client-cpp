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

#include "aether/ae_actions/select_client.h"

#include <cassert>

#include "aether/ae_actions/ae_actions_tele.h"
#include "aether/ae_actions/registration/registration.h"

namespace ae {
SelectClientAction::SelectClientAction(ActionContext action_context,
                                       Client::ptr const& client)
    : Action{action_context}, client_{client}, state_{State::kClientReady} {
  AE_TELED_DEBUG("Select loaded client");
  Action::Trigger();
}

#if AE_SUPPORT_REGISTRATION  // only if registration is supported
SelectClientAction::SelectClientAction(ActionContext action_context,
                                       Registration& registration)
    : Action{action_context},
      state_{State::kWaitRegistration},
      registered_sub_{
          registration.ResultEvent().Subscribe([this](auto& action) {
            client_ = action.client();
            state_ = State::kClientRegistered;
          })},
      reg_failed_sub_{registration.ErrorEvent().Subscribe(
          [this](auto&) { state_ = State::kClientRegistrationError; })},
      state_changed_sub_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELED_DEBUG("Waiting for client registration");
}

#else  // or init like there is not client to select
SelectClientAction::SelectClientAction(ActionContext action_context,
                                       Registration&)
    : SelectClientAction{action_context} {}
#endif

SelectClientAction::SelectClientAction(ActionContext action_context)
    : Action{action_context}, state_{State::kNoClientToSelect} {
  AE_TELED_DEBUG("No clients to select");
  Action::Trigger();
}

ActionResult SelectClientAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitRegistration:
        break;
      case State::kClientRegistrationError:
      case State::kNoClientToSelect:
        return ActionResult::Error();
      case State::kClientReady:
      case State::kClientRegistered:
        return ActionResult::Result();
    }
  }
  return {};
}

Client::ptr SelectClientAction::client() const {
  auto client_ptr = client_.Lock();
  assert(client_ptr);
  return client_ptr;
}

SelectClientAction::State SelectClientAction::state() const { return state_; }

}  // namespace ae
