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

#include "aether/aether_app.h"

#include "registrator/registrator_config.h"

namespace ae::registrator {
class RegistratorAction : public Action<RegistratorAction> {
  enum class State : std::uint8_t {
    kRegistration,
    kResult,
    kError,
  };

 public:
  explicit RegistratorAction(ActionContext action_context,
                             RcPtr<AetherApp> const& aether_app,
                             RegistratorConfig const& registrator_config);
  UpdateStatus Update();

 private:
  void RegisterClients();

  Aether* aether_;
  RegistratorConfig registrator_config_;

  std::size_t clients_registered_{0};

  MultiSubscription registration_sub_;
  StateMachine<State> state_;
  Subscription state_changed_;
};

}  // namespace ae::registrator

#endif  // TOOLS_REGISTRATOR_REGISTRATOR_ACTION_H_
