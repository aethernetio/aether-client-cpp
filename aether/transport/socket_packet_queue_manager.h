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

#ifndef AETHER_TRANSPORT_SOCKET_PACKET_QUEUE_MANAGER_H_
#define AETHER_TRANSPORT_SOCKET_PACKET_QUEUE_MANAGER_H_

#include <queue>
#include <mutex>
#include <type_traits>

#include "aether/common.h"
#include "aether/actions/action_ptr.h"

#include "aether/transport/socket_packet_send_action.h"

namespace ae {
template <typename TSocketPacketSendAction,
          AE_REQUIRERS((std::is_base_of<SocketPacketSendAction,
                                        TSocketPacketSendAction>))>
class SocketPacketQueueManager
    : public Action<SocketPacketQueueManager<TSocketPacketSendAction>> {
 public:
  using BaseAction = Action<SocketPacketQueueManager<TSocketPacketSendAction>>;

  explicit SocketPacketQueueManager(ActionContext action_context)
      : BaseAction{std::move(action_context)} {}

  AE_CLASS_NO_COPY_MOVE(SocketPacketQueueManager)

  UpdateStatus Update() {
    if (stopped_) {
      return UpdateStatus::Stop();
    }

    Send();
    return {};
  }

  ActionPtr<SocketPacketSendAction> AddPacket(
      ActionPtr<TSocketPacketSendAction>&& packet_send_action) {
    auto lock = std::unique_lock{queue_lock_};
    queue_.emplace(packet_send_action);
    BaseAction::Trigger();
    return std::move(packet_send_action);
  }

  /**
   * \brief Triggers send on queued action.
   * This invoked automatically in every Update or, if requires, immediately on
   * socket write event (\see IPoller)
   */
  void Send() {
    auto lock = std::lock_guard{queue_lock_};

    // select current active send action
    if (!current_active_) {
      if (queue_.empty()) {
        return;
      }
      current_active_ = queue_.front();
      queue_.pop();
    }

    // call send on current active action
    if (auto state = current_active_->state();
        (state == WriteAction::State::kQueued) ||
        (state == WriteAction::State::kInProgress)) {
      current_active_->Send();
    }
    // if after send state is changed from progress to something else move to
    // the next action
    if (current_active_->state() != WriteAction::State::kInProgress) {
      current_active_.reset();
      BaseAction::Trigger();
    }
  }

  void Stop() {
    stopped_ = true;
    BaseAction::Trigger();
  }

 private:
  mutable std::mutex queue_lock_;
  std::queue<ActionPtr<TSocketPacketSendAction>> queue_;
  ActionPtr<TSocketPacketSendAction> current_active_;
  std::atomic_bool stopped_{false};
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_SOCKET_PACKET_QUEUE_MANAGER_H_
