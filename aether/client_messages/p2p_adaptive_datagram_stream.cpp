/*
 * Copyright 2026 Aethernet Inc.
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

#include "aether/client_messages/p2p_adaptive_datagram_stream.h"

#include <optional>
#include <utility>
#include "aether/client_messages/p2p_stream_write_budget.h"

#include "aether/write_action/failed_write_action.h"

namespace ae {

// This carrier checks the actual selected endpoints, rather than inferring UDP
// from an aggregate is_reliable flag (a mixed path also reports false). Data
// and feedback both pass through the same check on every write and receive.
struct P2pAdaptiveDatagramStream::Impl final : ByteIStream {
  Impl(AeContext const& context, Ptr<Client> const& client, Uid destination,
       P2pPortHandle handle, adaptive_datagram::Config config,
       RequestPolicy::Variant send_policy)
      : context{context},
        p2p{context, client, std::move(destination), std::move(handle),
            std::move(send_policy)},
        output{context},
        adaptive{context, config},
        receive{p2p.out_data_event().Subscribe([this](auto const& data) {
          if (stream_info().link_state == LinkState::kLinked) {
            output.Emit(data);
          }
        })} {
    adaptive.LinkOut(*this);
  }

  WriteAction& Write(DataBuffer&& data) override {
    auto const info = stream_info();
    if (!info.is_writable || data.size() > info.max_element_size) {
      if (!failed || failed->is_finished()) {
        failed.emplace(context);
      }
      return *failed;
    }
    return p2p.Write(std::move(data));
  }

  StreamInfo stream_info() const override {
    auto const path = p2p.InspectSendPathTransport();
    auto info = path.aggregated;
    info.max_element_size =
        P2pStreamWritePayloadLimitFromChannelMax(info.max_element_size);
    info.rec_element_size =
        P2pStreamWritePayloadLimitFromChannelMax(info.rec_element_size);
    if (!IsSupportedSendPath(path)) {
      info.is_writable = false;
      if (info.link_state == LinkState::kLinked) {
        info.link_state = LinkState::kLinkError;
      }
    }
    return info;
  }
  StreamUpdateEvent const& stream_update_event() override {
    return p2p.stream_update_event();
  }
  OutDataEvent const& out_data_event() override { return output; }
  void Restream() override { p2p.Restream(); }

  AeContext context;
  P2pStream p2p;
  OutDataEvent output;
  std::optional<FailedWriteAction> failed;
  AdaptiveDatagramStream adaptive;
  Subscription receive;
};

P2pAdaptiveDatagramStream::P2pAdaptiveDatagramStream(
    AeContext const& context, Ptr<Client> const& client, Uid destination,
    P2pPortHandle handle, adaptive_datagram::Config config,
    RequestPolicy::Variant send_policy)
    : impl_{std::make_unique<Impl>(context, client, std::move(destination),
                                   std::move(handle), config,
                                   std::move(send_policy))} {}
P2pAdaptiveDatagramStream::~P2pAdaptiveDatagramStream() = default;
WriteAction& P2pAdaptiveDatagramStream::Write(DataBuffer&& data) {
  return impl_->adaptive.Write(std::move(data));
}
StreamInfo P2pAdaptiveDatagramStream::stream_info() const {
  return impl_->adaptive.stream_info();
}
P2pAdaptiveDatagramStream::StreamUpdateEvent const&
P2pAdaptiveDatagramStream::stream_update_event() {
  return impl_->adaptive.stream_update_event();
}
P2pAdaptiveDatagramStream::OutDataEvent const&
P2pAdaptiveDatagramStream::out_data_event() {
  return impl_->adaptive.out_data_event();
}
void P2pAdaptiveDatagramStream::Restream() { impl_->adaptive.Restream(); }
AdaptiveDatagramStream::DeliveryEvent const&
P2pAdaptiveDatagramStream::delivery_event() {
  return impl_->adaptive.delivery_event();
}
adaptive_datagram::StreamStatistics P2pAdaptiveDatagramStream::statistics()
    const {
  return impl_->adaptive.statistics();
}
adaptive_datagram::RateControllerMetrics const&
P2pAdaptiveDatagramStream::metrics() const {
  return impl_->adaptive.metrics();
}
std::uint64_t P2pAdaptiveDatagramStream::rate_bytes_per_second() const {
  return impl_->adaptive.rate_bytes_per_second();
}
bool P2pAdaptiveDatagramStream::valid() const {
  return impl_->adaptive.valid();
}
P2pStream::SendPathTransportView
P2pAdaptiveDatagramStream::InspectSendPathTransport() const {
  return impl_->p2p.InspectSendPathTransport();
}

bool P2pAdaptiveDatagramStream::IsSupportedSendPath(
    P2pStream::SendPathTransportView const& path) {
  return path.aggregated.link_state == LinkState::kLinked &&
         !path.aggregated.is_reliable && path.linked_udp_endpoint &&
         !path.linked_tcp_endpoint;
}

}  // namespace ae
