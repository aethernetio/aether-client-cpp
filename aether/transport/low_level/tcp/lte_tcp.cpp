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

#include "aether/transport/low_level/tcp/lte_tcp.h"

#if defined LTE_TCP_TRANSPORT_ENABLED

#  include "aether/mstream_buffers.h"
#  include "aether/mstream.h"
#  include "aether/format/format.h"
#  include "aether/transport/transport_tele.h"

namespace ae {
  
LteTcpTransport::LteTcpTransport(ActionContext action_context,
                                   IPoller::ptr poller,
                                   IpAddressPort const &endpoint)
    : action_context_{action_context},
      poller_{std::move(poller)},
      endpoint_{endpoint},
      connection_info_{},
      socket_packet_queue_manager_{action_context_} {
  AE_TELE_DEBUG(TcpTransport);
}

LteTcpTransport::~LteTcpTransport() { Disconnect(); }

void LteTcpTransport::Connect() {}

ConnectionInfo const &LteTcpTransport::GetConnectionInfo() const {
  return connection_info_;
}

ITransport::ConnectionSuccessEvent::Subscriber
LteTcpTransport::ConnectionSuccess() {
  return connection_success_event_;
}

ITransport::ConnectionErrorEvent::Subscriber
LteTcpTransport::ConnectionError() {
  return connection_error_event_;
}

ITransport::DataReceiveEvent::Subscriber LteTcpTransport::ReceiveEvent() {
  return data_receive_event_;
}

ActionView<PacketSendAction> LteTcpTransport::Send(DataBuffer data,
                                                   TimePoint current_time) {
  AE_TELE_DEBUG(TcpTransportSend, "Send data size {} at {:%Y-%m-%d %H:%M:%S}",
                data.size(), current_time);
  
  ae::ActionView<PacketSendAction> send_action {};
      
  return send_action;
}

void LteTcpTransport::Disconnect() {}

}  // namespace ae
#endif  // defined LTE_TCP_TRANSPORT_ENABLED