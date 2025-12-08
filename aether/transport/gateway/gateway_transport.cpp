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

#include "aether/transport/gateway/gateway_transport.h"

#if AE_SUPPORT_GATEWAY

namespace ae {
ClientId GatewayTransport::next_client_id_ = 0;

GatewayTransport::GatewayTransport(ServerKind server_kind,
                                   IGatewayDevice& gateway_device)
    : server_kind_{std::move(server_kind)},
      gateway_device_{&gateway_device},
      client_id_{next_client_id_++},
      stream_info_{
          /* .rec_element_size = */ {},
          /* .max_element_size = */ {},
          /* .is_reliable = */ false,
          /* .link_state = */ LinkState::kLinked,
          /* .is_writable = */ true,
      } {
  from_server_sub_ = gateway_device_->from_server_event().Subscribe(
      [this](ClientId cid, DataBuffer const& data) {
        if (client_id_ != cid) {
          return;
        }
        out_data_event_.Emit(data);
      });
}

ActionPtr<StreamWriteAction> GatewayTransport::Write(DataBuffer&& in_data) {
  return std::visit(
      [this, &in_data](auto const& server) {
        return gateway_device_->ToServer(client_id_, server,
                                         std::move(in_data));
      },
      server_kind_);
}

GatewayTransport::StreamUpdateEvent::Subscriber
GatewayTransport::stream_update_event() {
  return EventSubscriber{stream_update_event_};
}

StreamInfo GatewayTransport::stream_info() const { return stream_info_; }

GatewayTransport::OutDataEvent::Subscriber GatewayTransport::out_data_event() {
  return EventSubscriber{out_data_event_};
}

void GatewayTransport::Restream() {
  stream_info_.link_state = LinkState::kLinkError;
  stream_update_event_.Emit();
}
}  // namespace ae
#endif
