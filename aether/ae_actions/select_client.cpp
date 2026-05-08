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
#include "aether/client.h"
#include "aether/registration/registration.h"

#include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
SelectClientAction::SelectClientAction(AeContext const& ae_context,
                                       Client::ptr client)
    : ae_context_{ae_context} {
  AE_TELED_DEBUG("Select loaded client");
  task_sub_ = ae_context_.scheduler().Task([this, c{std::move(client)}]() {
    client_selected_event_.Emit(c);
    Finish();
  });
}

#if AE_SUPPORT_REGISTRATION  // only if registration is supported
SelectClientAction::SelectClientAction(AeContext const& ae_context,
                                       Aether& aether,
                                       Registration& registration,
                                       std::string client_id)
    : ae_context_{ae_context}, client_id_{std::move(client_id)} {
  AE_TELED_DEBUG("Waiting for client registration");
  registration_sub_ = registration.registration().Subscribe(
      [this, aeth{&aether}](auto const& res) {
        if (res) {
          auto client = aeth->CreateClient(res.value(), client_id_);
          client_selected_event_.Emit(client);
        } else {
          selection_failed_event_.Emit();
        }
        Finish();
      });
}
#endif

SelectClientAction::SelectClientAction(AeContext const& ae_context)
    : ae_context_{ae_context} {
  AE_TELED_DEBUG("No clients to select");
  task_sub_ = ae_context_.scheduler().Task([this]() {
    selection_failed_event_.Emit();
    Finish();
  });
}

SelectClientAction::ClientSelectedEvent::Subscriber
SelectClientAction::client_selected() {
  return EventSubscriber{client_selected_event_};
}
SelectClientAction::SelectionFailedEvent::Subscriber
SelectClientAction::selection_failed() {
  return EventSubscriber{selection_failed_event_};
}

}  // namespace ae
