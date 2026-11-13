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

#include <etl/circular_buffer.h>

#include "aether/ae_context.h"
#include "aether/common.h"
#include "aether/config.h"
#include "aether/transport/packet_send_action.h"

#include "aether/transport/transport_tele.h"

namespace ae {
template <typename T, std::size_t MaxSize>
  requires(std::is_base_of_v<PacketSendAction, T>)
class PacketQueueManager {
 public:
  static constexpr std::size_t kMaxSize = MaxSize;

  explicit PacketQueueManager(AeContext const& ae_context) noexcept
      : ae_context_{ae_context} {}

  ~PacketQueueManager() { AETHER_TELED_DEBUG("Packet queue manager remove"); }

  AE_CLASS_NO_COPY_MOVE(PacketQueueManager)

  template <typename... Args>
  T* AddPacket(Args&&... args) {
    std::unique_lock lock{queue_lock_};
    DrainFinishedLocked();
    RetryDeferredTerminalsLocked(lock);
    if (queue_.size() == kMaxSize) {
      // queue is full, drop the packet
      return nullptr;
    }
    queue_.push(T{std::forward<Args>(args)...});
    Enqueue();
    return &queue_.back();
  }

  /**
   * \brief Triggers send on queued action.
   */
  void Send() {
    std::unique_lock lock{queue_lock_};
    DrainFinishedLocked();
    RetryDeferredTerminalsLocked(lock);

    while (index_ < queue_.size()) {
      if (queue_[index_].is_done()) {
        ++index_;
        continue;
      }
      std::size_t const work_index = index_;
      queue_[work_index].Send();
      bool const send_completed = queue_[work_index].is_done();
      EmitDeferredTerminalUnlocked(lock, work_index);
      if (index_ >= queue_.size()) {
        break;
      }
      if (queue_[index_].is_done()) {
        ++index_;
        continue;
      }
      if (index_ == work_index && !send_completed) {
        if (queue_[index_].re_enqueue()) {
          Enqueue();
        }
        break;
      }
      continue;
    }
  }

 private:
  void DrainFinishedLocked() {
    std::size_t removed = 0;
    while (!queue_.empty() && queue_.front().is_finished()) {
      queue_.pop();
      ++removed;
    }
    index_ = (index_ <= removed) ? 0 : index_ - removed;
  }

  void EmitDeferredTerminalUnlocked(std::unique_lock<std::mutex>& lock,
                                    std::size_t idx) {
    if (idx >= queue_.size()) {
      return;
    }
    T& slot = queue_[idx];
    if (!slot.is_done() || slot.is_finished()) {
      return;
    }
    lock.unlock();
    slot.TryEmitDeferredTerminal();
    lock.lock();
  }

  void RetryDeferredTerminalsLocked(std::unique_lock<std::mutex>& lock) {
    for (std::size_t i = 0; i < queue_.size();) {
      if (queue_[i].is_done() && !queue_[i].is_finished()) {
        EmitDeferredTerminalUnlocked(lock, i);
        DrainFinishedLocked();
        if (i >= queue_.size()) {
          break;
        }
        if (queue_[i].is_done() && !queue_[i].is_finished()) {
          ++i;
          continue;
        }
        i = index_;
        continue;
      }
      ++i;
    }
  }

  void Enqueue() {
    ae_context_.scheduler().TaskIfAbsent(enqueue_sub_, [&]() {
      ae_context_.scheduler().Cancel(enqueue_sub_);
      Send();
    });
  }

  AeContext ae_context_;
  TaskSubscription enqueue_sub_;
  mutable std::mutex queue_lock_;
  etl::circular_buffer<T, kMaxSize> queue_;
  std::size_t index_ = 0;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_SOCKET_PACKET_QUEUE_MANAGER_H_
