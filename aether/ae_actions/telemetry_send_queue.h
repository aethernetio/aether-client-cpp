/*
 * Copyright 2026 Aethernet Inc.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef AETHER_AE_ACTIONS_TELEMETRY_SEND_QUEUE_H_
#define AETHER_AE_ACTIONS_TELEMETRY_SEND_QUEUE_H_

#include <functional>
#include <utility>
#include <vector>

#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"
#include "aether/work_cloud_api/telemetric.h"
#include "aether/write_action/write_action.h"

namespace ae::telemetry_internal {
// One owner thread, as with the telemetry/cloud task scheduler. The queue owns
// its snapshot and has at most one transport action and one continuation.
// A rejected/failed fragment aborts this snapshot; the source archive is never
// consumed, so a later request can send a complete new snapshot.
class FragmentSendQueue {
 public:
  using Sender = std::function<WriteAction*(Telemetric&&)>;
  using Completion = std::function<void(bool)>;
  FragmentSendQueue(TaskScheduler& scheduler, Sender sender,
                    Completion completion)
      : scheduler_{&scheduler},
        sender_{std::move(sender)},
        completion_{std::move(completion)} {}
  FragmentSendQueue(FragmentSendQueue const&) = delete;
  FragmentSendQueue& operator=(FragmentSendQueue const&) = delete;
  ~FragmentSendQueue() { Cancel(); }

  bool busy() const { return busy_; }
  bool Start(std::vector<Telemetric> payloads) {
    if (busy_ || payloads.empty()) return false;
    payloads_ = std::move(payloads);
    position_ = 0;
    busy_ = true;
    SendNext();
    return true;
  }
  void Cancel() {
    write_sub_.Reset();
    continuation_.Reset();
    payloads_.clear();
    busy_ = false;
  }

 private:
  void SendNext() {
    auto* action = sender_(std::move(payloads_[position_]));
    // A completed action has no retained status to subscribe to. Treat it as
    // a failed enqueue instead of hanging or assuming delivery succeeded.
    if (action == nullptr || action->is_finished()) {
      Complete(false);
      return;
    }
    write_sub_ = action->status_event().Subscribe([this](auto status) {
      write_sub_.Reset();
      // Status is emitted before Action::Finish queues its cleanup. Two
      // scheduler turns put the next enqueue after those cleanup tasks even
      // when this status callback queued its continuation first.
      continuation_ = scheduler_->Task([this, status]() {
        continuation_.Reset();
        continuation_ = scheduler_->Task([this, status]() {
          continuation_.Reset();
          if (status != WriteAction::Status::kSuccess) {
            Complete(false);
          } else if (++position_ == payloads_.size()) {
            Complete(true);
          } else {
            SendNext();
          }
        });
        if (!continuation_) Complete(false);
      });
      if (!continuation_) Complete(false);
    });
  }
  void Complete(bool success) {
    Cancel();
    completion_(success);
  }
  TaskScheduler* scheduler_;
  Sender sender_;
  Completion completion_;
  std::vector<Telemetric> payloads_;
  std::size_t position_{};
  bool busy_{};
  Subscription write_sub_;
  TaskSubscription continuation_;
};
}  // namespace ae::telemetry_internal
#endif  // AETHER_AE_ACTIONS_TELEMETRY_SEND_QUEUE_H_
