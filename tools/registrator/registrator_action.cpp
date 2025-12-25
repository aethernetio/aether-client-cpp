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

namespace ae::registrator {
RegistratorAction::RegistratorAction(
    ActionContext action_context, RcPtr<AetherApp> const& aether_app,
    std::vector<reg::ClientConfig> client_configs)
    : Action{action_context},
      aether_{aether_app->aether()},
      client_configs_{std::move(client_configs)},
      state_{State::kRegistration} {
  AE_TELED_INFO("RegistratorAction");
  state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
}

UpdateStatus RegistratorAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kRegistration:
        RegisterClients();
        break;
      case State::kResult:
        return UpdateStatus::Result();
      case State::kError:
        return UpdateStatus::Error();
    }
  }

  return {};
}

/**
 * \brief Perform a client registration.
 * We need a two clients for this test.
 */
void RegistratorAction::RegisterClients() {
  AE_TELED_INFO("Client registration");
#if not AE_SUPPORT_REGISTRATION
  // registration should be supported for this tool
  assert(false);
#else
  auto aether_ptr = aether_.Lock();
  assert(aether_ptr);
  for (auto const& c : client_configs_) {
    auto parent_uid = ae::Uid::FromString(c.parent_uid);
    assert(!parent_uid.empty());
    auto select_action = aether_ptr->SelectClient(parent_uid, c.client_id);
    registration_sub_ += select_action->StatusEvent().Subscribe(ActionHandler{
        OnResult{[this](auto const&) {
          if (++clients_registered_ == client_configs_.size()) {
            state_ = State::kResult;
          }
        }},
        OnError{[this]() {
          AE_TELED_ERROR("Registration error");
          state_ = State::kError;
        }},
    });
  }
#endif
}
}  // namespace ae::registrator
