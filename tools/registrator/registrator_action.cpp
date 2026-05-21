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

#include "aether/actions/action_context.h"

#include "aether/registration/registration.h"

namespace ae::reg {
RegistratorAction::RegistratorAction(
    ae::AeContext const& ae_context, RcPtr<AetherApp> const& aether_app,
    std::vector<reg::ClientConfig> const& client_configs)
    : ae_context_{ae_context} {
  AE_TELED_INFO("RegistratorAction");
  RegisterClients(aether_app->aether(), client_configs);
}

RegistratorAction::RegisteredEvent::Subscriber
RegistratorAction::registered_event() {
  return EventSubscriber{registered_event_};
}
RegistratorAction::FailedEvent::Subscriber RegistratorAction::failed_event() {
  return EventSubscriber{failed_event_};
}

/**
 * \brief Perform a client registration.
 * We need a two clients for this test.
 */
void RegistratorAction::RegisterClients(
    ae::Aether::ptr const& aether,
    std::vector<reg::ClientConfig> const& client_configs) {
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

    registration_sub_ +=
        aether->RegisterClient(c.client_id, parent_uid)
            .registration()
            .Subscribe([this, clients_count,
                        client_id{&c.client_id}](auto const& res) {
              if (res) {
                registered_clients_.emplace_back(
                    RegisteredClient{res.value(), *client_id});
                if (registered_clients_.size() == clients_count) {
                  registered_event_.Emit(std::move(registered_clients_));
                }
              } else {
                AE_TELED_ERROR("Registration error");
                failed_event_.Emit();
              }
            });
  }
#endif
}
}  // namespace ae::reg
