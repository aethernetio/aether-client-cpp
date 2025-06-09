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

#include "aether/common.h"
#include "aether/aether.h"
#include "aether/literal_array.h"

namespace ae::registrator {
RegistratorAction::RegistratorAction(
    Ptr<AetherApp> const& aether_app,
    RegistratorConfig const& registrator_config)
    : Action{*aether_app},
      aether_{aether_app->aether()},
      registrator_config_{registrator_config},
      state_{State::kRegistration},
      state_changed_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELED_INFO("Registration test");
}

ActionResult RegistratorAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kRegistration:
        RegisterClients();
        break;
      case State::kResult:
        return ActionResult::Result();
      case State::kError:
        return ActionResult::Error();
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
  for (auto const& p : registrator_config_.GetParents()) {
    auto parent_uid = ae::Uid::FromString(p.uid_str);
    auto clients_num = p.clients_num;

    for (auto i = 0; i < clients_num; i++) {
      auto reg_action = aether_ptr->RegisterClient(parent_uid);

      registration_subscriptions_.Push(
          reg_action->ResultEvent().Subscribe([this](auto const&) {
            if (++clients_registered_ ==
                registrator_config_.GetClientsTotal()) {
              state_ = State::kResult;
            }
          }),
          reg_action->ErrorEvent().Subscribe([this](auto const&) {
            AE_TELED_ERROR("Registration error");
            state_ = State::kError;
          }));
    }
  }
#endif
}
}  // namespace ae::registrator
