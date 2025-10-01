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
#ifndef AETHER_CHANNELS_ETHERNET_TRANSPORT_BUILDER_H_
#define AETHER_CHANNELS_ETHERNET_TRANSPORT_BUILDER_H_

#include <memory>

#include "aether/obj/obj_ptr.h"
#include "aether/ptr/ptr_view.h"
#include "aether/types/address.h"

#include "aether/transport/itransport_stream_builder.h"

namespace ae {
class IPoller;

class EthernetTransportBuilder final : public ITransportStreamBuilder {
 public:
  EthernetTransportBuilder(ActionContext action_context,
                           ObjPtr<IPoller> const& poller,
                           IpAddressPortProtocol address_port_protocol);

  std::unique_ptr<ByteIStream> BuildTransportStream() override;

 private:
  std::unique_ptr<ByteIStream> BuildTcp();
  std::unique_ptr<ByteIStream> BuildUdp();

  ActionContext action_context_;
  PtrView<IPoller> poller_;
  IpAddressPortProtocol address_port_protocol_;
};
}  // namespace ae

#endif  // AETHER_CHANNELS_ETHERNET_TRANSPORT_BUILDER_H_
