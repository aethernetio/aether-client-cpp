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

#ifndef AETHER_TRANSPORT_LOW_LEVEL_TCP_SOCKET_PACKET_QUEUE_MANAGER_H_
#define AETHER_TRANSPORT_LOW_LEVEL_TCP_SOCKET_PACKET_QUEUE_MANAGER_H_

#include <queue>
#include <mutex>
#include <type_traits>

#include "aether/common.h"
#include "aether/actions/action_view.h"
#include "aether/actions/action_list.h"
#include "aether/events/multi_subscription.h"

#include "aether/transport/actions/packet_send_action.h"
#include "aether/transport/low_level/tcp/socket_packet_send_action.h"

namespace ae {
template <typename TSocketPacketSendAction,
          AE_REQUIRERS((std::is_base_of<SocketPacketSendAction,
                                        TSocketPacketSendAction>))>
class SocketPacketQueueManager
    : Action<SocketPacketQueueManager<TSocketPacketSendAction>> {
 public:
  using BaseAction = Action<SocketPacketQueueManager<TSocketPacketSendAction>>;

  explicit SocketPacketQueueManager(ActionContext action_context)
      : BaseAction{std::move(action_context)} {}

  AE_CLASS_NO_COPY_MOVE(SocketPacketQueueManager)

  TimePoint Update(TimePoint current_time) override {
    Send();
    return current_time;
  }

  ActionView<SocketPacketSendAction> AddPacket(
      TSocketPacketSendAction&& packet_send_action) {
    auto lock = std::unique_lock{queue_lock_};
    auto view = actions_.Add(std::move(packet_send_action));
    queue_.emplace(view);
    on_sent_subs_.Push(
        view->ResultEvent().Subscribe([this](auto const&) { Send(); }));
    BaseAction::Trigger();
    return view;
  }

  /**
   * \brief Triggers send on queued action.
   * This invoked automatically in every Update or, if requires, immediately on
   * socket write event (\see IPoller)
   */
  void Send() {
    auto lock = std::lock_guard{queue_lock_};
    while (!queue_.empty()) {
      auto action = queue_.front();
      if (!action) {
        queue_.pop();
        continue;
      }
      if (auto state = action->state().get();
          (state == PacketSendAction::State::kQueued) ||
          (state == PacketSendAction::State::kProgress)) {
        action->Send();
      }
      if (action->state() == PacketSendAction::State::kProgress) {
        break;
      }
      queue_.pop();
    }
  }

  bool empty() const {
    auto lock = std::lock_guard{queue_lock_};
    return queue_.empty();
  }

 private:
  mutable std::mutex queue_lock_;
  ActionStore<TSocketPacketSendAction> actions_;
  std::queue<ActionView<TSocketPacketSendAction>> queue_;
  MultiSubscription on_sent_subs_;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_LOW_LEVEL_TCP_SOCKET_PACKET_QUEUE_MANAGER_H_
