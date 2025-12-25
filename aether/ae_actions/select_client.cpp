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

#include "aether/aether.h"
#include "aether/registration/registration.h"

#include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
SelectClientAction::SelectClientAction(ActionContext action_context,
                                       Client::ptr const& client)
    : Action{action_context}, client_{client}, state_{State::kClientReady} {
  AE_TELED_DEBUG("Select loaded client");
  Action::Trigger();
}

#if AE_SUPPORT_REGISTRATION  // only if registration is supported
SelectClientAction::SelectClientAction(ActionContext action_context,
                                       Aether& aether,
                                       ActionPtr<Registration> registration,
                                       std::string client_id)
    : Action{action_context},
      state_{State::kWaitRegistration},
      client_id_{std::move(client_id)},
      registration_{std::move(registration)} {
  AE_TELED_DEBUG("Waiting for client registration");
  registration_sub_ = registration_->StatusEvent().Subscribe(ActionHandler{
      OnResult{[this, aeth{&aether}](auto& action) {
        client_ = aeth->CreateClient(action.client_config(), client_id_);
        state_ = State::kClientRegistered;
      }},
      OnError{[this]() { state_ = State::kClientRegistrationError; }},
  });

  state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
}
#endif

SelectClientAction::SelectClientAction(ActionContext action_context)
    : Action{action_context}, state_{State::kNoClientToSelect} {
  AE_TELED_DEBUG("No clients to select");
  Action::Trigger();
}

UpdateStatus SelectClientAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitRegistration:
        break;
      case State::kClientRegistrationError:
      case State::kNoClientToSelect:
        return UpdateStatus::Error();
      case State::kClientReady:
      case State::kClientRegistered:
        return UpdateStatus::Result();
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
