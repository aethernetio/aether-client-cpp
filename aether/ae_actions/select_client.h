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

#ifndef AETHER_AE_ACTIONS_SELECT_CLIENT_H_
#define AETHER_AE_ACTIONS_SELECT_CLIENT_H_

#include "aether/config.h"
#include "aether/ae_context.h"
#include "aether/obj/obj_ptr.h"
#include "aether-miscpp/types/result.h"
#include "aether/events/events.h"
#include "aether/actions/action.h"
#include "aether/events/event_subscription.h"

namespace ae {
class Aether;
class Client;
class Registration;

class SelectClientAction final : public Action {
 public:
  using ResultEvent = Event<void(Result<ObjPtr<Client>, int>)>;

  /**
   * \brief Create with client already ready.
   */
  SelectClientAction(AeContext const& ae_context, ObjPtr<Client> client);

#if AE_SUPPORT_REGISTRATION
  /**
   * \brief Wait for client registration or error.
   */
  SelectClientAction(AeContext const& ae_context, Aether& aether,
                     Registration& registration, std::string client_id);
#endif

  /**
   * \brief There is not client to select, throw error.
   */
  explicit SelectClientAction(AeContext const& ae_context);

  ResultEvent::Subscriber result_event();

 private:
  AeContext ae_context_;
  TaskSubscription task_sub_;
  ResultEvent result_event_;

#if AE_SUPPORT_REGISTRATION
  std::string client_id_;
  Subscription registration_sub_;
#endif
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_SELECT_CLIENT_H_
