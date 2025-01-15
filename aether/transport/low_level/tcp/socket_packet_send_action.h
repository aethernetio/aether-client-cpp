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

#ifndef AETHER_TRANSPORT_LOW_LEVEL_TCP_SOCKET_PACKET_SEND_ACTION_H_
#define AETHER_TRANSPORT_LOW_LEVEL_TCP_SOCKET_PACKET_SEND_ACTION_H_

#include "aether/transport/actions/packet_send_action.h"

namespace ae {
class SocketPacketSendAction : public PacketSendAction {
 public:
  using PacketSendAction::PacketSendAction;

  TimePoint Update(TimePoint current_time) override;

  void Stop() override;

  // Trigger event to send data
  virtual void Send() = 0;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_LOW_LEVEL_TCP_SOCKET_PACKET_SEND_ACTION_H_
