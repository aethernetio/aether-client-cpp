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

#ifndef AETHER_AE_ACTIONS_PREPARE_FOR_SHUTDOWN_H_
#define AETHER_AE_ACTIONS_PREPARE_FOR_SHUTDOWN_H_

#include <chrono>
#include <cstdint>

#include "aether/ae_context.h"
#include "aether/actions/action.h"
#include "aether/events/event_subscription.h"
#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"
#include "aether/write_action/write_action.h"

namespace ae {
class Client;

inline constexpr std::chrono::milliseconds kPrepareForShutdownTimeout{500};

enum class PrepareForShutdownStatus : std::uint8_t {
  kWriteSuccess = 0,
  kWriteFail = 1,
  kTimeout = 2,
  kNoCloud = 3,
};

// Ordered graceful cloud shutdown for one Client:
//   STOP_PING -> setNextReadDelay(0) -> WRITE_DONE|FAIL|TIMEOUT
class PrepareForShutdown final : public Action {
 public:
  using ResultEvent = Event<void(PrepareForShutdownStatus)>;

  PrepareForShutdown(AeContext const& ae_context, Client& client);

  AE_CLASS_NO_COPY_MOVE(PrepareForShutdown)

  ResultEvent::Subscriber result_event() noexcept;
  PrepareForShutdownStatus status() const noexcept { return status_; }
  bool ping_stopped_before_zero() const noexcept {
    return ping_stopped_before_zero_;
  }
  bool delay_zero_sent() const noexcept { return delay_zero_sent_; }

 private:
  void Complete(PrepareForShutdownStatus status);

  AeContext ae_context_;
  Client* client_{nullptr};
  ResultEvent result_event_;
  Subscription write_sub_;
  TaskSubscription timeout_sub_;
  PrepareForShutdownStatus status_{PrepareForShutdownStatus::kNoCloud};
  bool finished_{false};
  bool ping_stopped_before_zero_{false};
  bool delay_zero_sent_{false};
};

}  // namespace ae

#endif  // AETHER_AE_ACTIONS_PREPARE_FOR_SHUTDOWN_H_
