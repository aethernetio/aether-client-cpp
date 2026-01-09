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

namespace ae::reg {
RegistratorAction::RegistratorAction(
    ActionContext action_context, RcPtr<AetherApp> const& aether_app,
    std::vector<reg::ClientConfig> const& client_configs)
    : Action{action_context}, state_{State::kWait} {
  AE_TELED_INFO("RegistratorAction");
  state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
  RegisterClients(*aether_app->aether(), client_configs);
}

UpdateStatus RegistratorAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWait:
        break;
      case State::kResult:
        return UpdateStatus::Result();
      case State::kError:
        return UpdateStatus::Error();
    }
  }
  return {};
}

std::vector<RegisteredClient> const& RegistratorAction::registered_clients()
    const {
  return registered_clients_;
}

/**
 * \brief Perform a client registration.
 * We need a two clients for this test.
 */
void RegistratorAction::RegisterClients(
    ae::Aether& aether, std::vector<reg::ClientConfig> const& client_configs) {
  AE_TELED_INFO("Client registration");
#if not AE_SUPPORT_REGISTRATION
  // registration should be supported for this tool
  assert(false);
#else

  std::size_t clients_count = client_configs.size();
  assert(clients_count > 0 && "No client configurations provided");

  for (auto const& c : client_configs) {
    auto parent_uid = ae::Uid::FromString(c.parent_uid);
    assert(!parent_uid.empty());
    auto reg_action = aether.RegisterClient(parent_uid);
    registration_sub_ += reg_action->StatusEvent().Subscribe(ActionHandler{
        OnResult{
            [this, clients_count, client_id{&c.client_id}](auto const& action) {
              registered_clients_.emplace_back(RegisteredClient{
                  action.client_config(),
                  *client_id,
              });
              if (registered_clients_.size() == clients_count) {
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
}  // namespace ae::reg
