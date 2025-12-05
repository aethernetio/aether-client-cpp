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

#ifndef AETHER_TRANSPORT_GATEWAY_GATEWAY_TRANSPORT_H_
#define AETHER_TRANSPORT_GATEWAY_GATEWAY_TRANSPORT_H_

#include <variant>

#include "aether/config.h"

#if AE_SUPPORT_GATEWAY

#  include "aether/types/server_id.h"
#  include "aether/stream_api/istream.h"
#  include "aether/gateway_api/server_endpoints.h"
#  include "aether/transport/gateway/gateway_device.h"

namespace ae {
class GatewayTransport : public ByteIStream {
 public:
  using ServerKind = std::variant<ServerId, ServerEndpoints>;

  GatewayTransport(ServerKind server_kind, IGatewayDevice& gateway_device);

  AE_CLASS_NO_COPY_MOVE(GatewayTransport)

  ActionPtr<StreamWriteAction> Write(DataBuffer&& in_data) override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

 private:
  static ClientId next_client_id_;

  ServerKind server_kind_;
  IGatewayDevice* gateway_device_;
  ClientId client_id_;

  Subscription from_server_sub_;
  StreamInfo stream_info_;
  StreamUpdateEvent stream_update_event_;
  OutDataEvent out_data_event_;
};
}  // namespace ae

#endif
#endif  // AETHER_TRANSPORT_GATEWAY_GATEWAY_TRANSPORT_H_
