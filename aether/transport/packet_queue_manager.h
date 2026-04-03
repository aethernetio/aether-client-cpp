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

#include <mutex>
#include <type_traits>

#include <third_party/etl/include/etl/circular_buffer.h>

#include "aether/ae_context.h"
#include "aether/transport/packet_send_action.h"

namespace ae {
template <typename T, std::size_t MaxSize = 10>
  requires(std::is_base_of_v<PacketSendAction, T>)
class PacketQueueManager {
 public:
  static constexpr std::size_t kMaxSize = MaxSize;

  explicit PacketQueueManager(AeContext const& ae_context) noexcept
      : ae_context_{ae_context} {}

  AE_CLASS_NO_COPY_MOVE(PacketQueueManager)

  PacketSendAction* AddPacket(T&& packet_sender) {
    auto lock = std::scoped_lock{queue_lock_};
    if (queue_.size() == kMaxSize) {
      // queue is full, drop the packet
      return nullptr;
    }
    queue_.push(std::move(packet_sender));
    Enqueue();
    return &queue_.back();
  }

  /**
   * \brief Triggers send on queued action.
   */
  void Send() {
    auto lock = std::scoped_lock{queue_lock_};
    // remove finished
    std::size_t i = 0;
    for (; i < queue_.size(); i++) {
      if (!queue_[i].is_finished()) {
        break;
      }
      queue_.pop();
    }
    index_ = (index_ <= i) ? 0 : index_ - i;

    // send queued packets
    for (; index_ < queue_.size(); index_++) {
      auto& current = queue_[index_];
      if (current.is_done()) {
        continue;
      }
      // make a send
      // then three states is possible:
      // it fully sent or failed - it's done
      // it requires additional pass - it's re_enqueue
      // it waits for completion - it's nothing, and next Send will return the
      // actual state.
      current.Send();
      if (current.is_done()) {
        continue;
      }
      if (current.re_enqueue()) {
        Enqueue();
      }
      // current waits for completion, wait for next Send call.
      break;
    }
  }

 private:
  void Enqueue() {
    ae_context_.scheduler().Task([&]() { Send(); });
  }

  AeContext ae_context_;
  mutable std::mutex queue_lock_;
  etl::circular_buffer<T, kMaxSize> queue_;
  std::size_t index_ = 0;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_SOCKET_PACKET_QUEUE_MANAGER_H_
