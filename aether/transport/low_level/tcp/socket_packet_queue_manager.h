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

#include "aether/actions/action_view.h"
#include "aether/actions/action_list.h"

#include "aether/transport/actions/packet_send_action.h"
#include "aether/transport/low_level/tcp/socket_packet_send_action.h"

namespace ae {
template <typename TSocketPacketSendAction,
          std::enable_if_t<std::is_base_of_v<SocketPacketSendAction,
                                             TSocketPacketSendAction>,
                           int> = 0>
class SocketPacketQueueManager {
 public:
  SocketPacketQueueManager() = default;

  ActionView<SocketPacketSendAction> AddPacket(
      TSocketPacketSendAction&& packet_send_action) {
    auto lock = std::unique_lock{queue_lock_};
    auto view = actions_.Add(std::move(packet_send_action));
    queue_.emplace(view);
    if (current_ == nullptr) {
      lock.unlock();
      Send();
    }
    return view;
  }

  // Triggers send on queued action
  void Send() {
    auto lock = std::lock_guard{queue_lock_};
    while (!queue_.empty()) {
      current_ = &queue_.front();
      if (*current_) {
        if (((*current_)->state() == PacketSendAction::State::kQueued) ||
            ((*current_)->state() == PacketSendAction::State::kProgress)) {
          (*current_)->Send();
        }
        if ((*current_)->state() == PacketSendAction::State::kProgress) {
          break;
        }
      }
      current_ = nullptr;
      queue_.pop();
    }
  }

  bool empty() const { return queue_.empty(); }

 private:
  std::mutex queue_lock_;
  ActionStore<TSocketPacketSendAction> actions_;
  std::queue<ActionView<TSocketPacketSendAction>> queue_;
  ActionView<TSocketPacketSendAction>* current_ = nullptr;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_LOW_LEVEL_TCP_SOCKET_PACKET_QUEUE_MANAGER_H_
