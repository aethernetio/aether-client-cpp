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

#include <vector>
#include <string>
#include <cstdint>

#include "aether/aether.h"
#include "aether/aether_app.h"
#include "aether/actions/action.h"

#include "registrator/registrator_config.h"

namespace ae::reg {
struct RegisteredClient {
  ae::ClientConfig config;
  std::string client_id;
};

class RegistratorAction : public Action<RegistratorAction> {
  enum class State : std::uint8_t {
    kWait,
    kResult,
    kError,
  };

 public:
  explicit RegistratorAction(
      ae::ActionContext action_context, RcPtr<ae::AetherApp> const& aether_app,
      std::vector<reg::ClientConfig> const& client_configs);
  UpdateStatus Update();

  std::vector<RegisteredClient> const& registered_clients() const;

 private:
  void RegisterClients(ae::Aether& aether,
                       std::vector<reg::ClientConfig> const& client_configs);

  std::vector<RegisteredClient> registered_clients_;

  MultiSubscription registration_sub_;
  StateMachine<State> state_;
};

}  // namespace ae::reg

#endif  // TOOLS_REGISTRATOR_REGISTRATOR_ACTION_H_
