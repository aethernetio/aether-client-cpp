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

#ifndef AETHER_TRANSPORT_MODEMS_SEND_QUEUE_POLLER_H_
#define AETHER_TRANSPORT_MODEMS_SEND_QUEUE_POLLER_H_

#include "aether/common.h"
#include "aether/actions/action.h"

#include "aether/transport/socket_packet_queue_manager.h"

namespace ae {
template <typename TSendAction>
class SendQueuePoller : public Action<SendQueuePoller<TSendAction>> {
 public:
  using BaseAction = Action<SendQueuePoller<TSendAction>>;

  explicit SendQueuePoller(ActionContext action_context, Duration interval)
      : BaseAction{action_context},
        socket_packet_queue_manager_{action_context},
        interval_{interval},
        stopped_{false} {}

  ActionPtr<SocketPacketSendAction> AddPacket(
      ActionPtr<TSendAction>&& packet_send_action) {
    return socket_packet_queue_manager_.AddPacket(
        std::move(packet_send_action));
  }

  UpdateStatus Update(TimePoint current_time) {
    if (stopped_) {
      return UpdateStatus::Stop();
    }
    socket_packet_queue_manager_.Send();
    // expected next send not longer than interval
    return UpdateStatus::Delay(current_time + interval_);
  }

  void Stop() {
    stopped_ = true;
    BaseAction::Trigger();
  }

 private:
  SocketPacketQueueManager<TSendAction> socket_packet_queue_manager_;
  Duration interval_;
  bool stopped_;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_MODEMS_SEND_QUEUE_POLLER_H_
