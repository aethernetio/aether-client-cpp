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
#include "aether/events/events.h"
#include "aether/events/multi_subscription.h"
#include "aether/actions/action2_.h"

#include "registrator/registrator_config.h"

namespace ae::reg {
struct RegisteredClient {
  ae::ClientConfig config;
  std::string client_id;
};

class RegistratorAction : public a2::Action {
 public:
  using RegisteredEvent = Event<void(std::vector<RegisteredClient>)>;
  using FailedEvent = Event<void()>;

  explicit RegistratorAction(
      ae::AeContext const& ae_context, RcPtr<ae::AetherApp> const& aether_app,
      std::vector<reg::ClientConfig> const& client_configs);

  RegisteredEvent::Subscriber registered_event();
  FailedEvent::Subscriber failed_event();

 private:
  void RegisterClients(ae::Aether::ptr const& aether,
                       std::vector<reg::ClientConfig> const& client_configs);

  AeContext ae_context_;
  std::vector<RegisteredClient> registered_clients_;

  RegisteredEvent registered_event_;
  FailedEvent failed_event_;

  MultiSubscription registration_sub_;
};

}  // namespace ae::reg

#endif  // TOOLS_REGISTRATOR_REGISTRATOR_ACTION_H_
