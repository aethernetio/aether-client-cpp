/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_AE_ACTIONS_ANNOUNCE_NEXT_PING_UNKNOWN_H_
#define AETHER_AE_ACTIONS_ANNOUNCE_NEXT_PING_UNKNOWN_H_

#include "aether/config.h"

#include <variant>

#include "aether-miscpp/types/result.h"

#include "aether/ae_context.h"
#include "aether/actions/action.h"
#include "aether/events/event_subscription.h"
#include "aether/events/events.h"
#include "aether/tasks/details/task_subsctiption.h"

namespace ae {
class Client;

enum class AnnounceNextPingUnknownError : int {
  kPingDisabled = 1,
  kNoPingManager = 2,
  kAnnounceFailed = 3,
};

class AnnounceNextPingUnknown final : public Action {
 public:
  using ResultEvent = Event<void(Result<std::monostate, int>)>;

  AnnounceNextPingUnknown(AeContext const& ae_context, Client& client);
  ~AnnounceNextPingUnknown() override;

  AE_CLASS_NO_COPY_MOVE(AnnounceNextPingUnknown)

  ResultEvent::Subscriber result_event() noexcept;

 private:
  void Start();
  void CompleteOk();
  void Fail(int code);

  AeContext ae_context_;
  Client* client_{nullptr};
  ResultEvent result_event_;
  Subscription announce_sub_;
  TaskSubscription start_sub_;
  bool finished_{false};
};

}  // namespace ae

#endif  // AETHER_AE_ACTIONS_ANNOUNCE_NEXT_PING_UNKNOWN_H_
