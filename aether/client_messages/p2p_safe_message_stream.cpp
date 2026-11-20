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

#include "aether/client_messages/p2p_safe_message_stream.h"

#include <algorithm>
#include <utility>

#if AE_SUPPORT_ADAPTIVE_DATAGRAM
#  include "aether/client_messages/p2p_adaptive_datagram_stream.h"
#endif
#include "aether/safe_stream/safe_stream.h"
#include "aether/stream_api/tied_gates.h"

namespace ae {
namespace {
#if AE_SUPPORT_ADAPTIVE_DATAGRAM
SafeStreamConfig AdaptiveTransportConfig(SafeStreamConfig config) {
  config.wait_for_writable_transport = true;
  config.minimum_ack_timeout =
      std::max(config.minimum_ack_timeout, Duration{1s});
  return config;
}
#endif

}  // namespace

P2pSafeStream::P2pSafeStream(AeContext const& ae_context,
                             SafeStreamConfig const& config,
                             std::shared_ptr<ByteIStream> transport)
    : sized_packet_gate_{ae_context},
      transport_{std::move(transport)},
      safe_stream_{std::make_unique<SafeStream>(ae_context, config)},
      out_data_event_{ae_context},
      receive_reset_event_{ae_context},
      out_data_sub_{
          sized_packet_gate_.out_data_event().Subscribe(
              [this](auto const& data) { out_data_event_.Emit(data); }),
          safe_stream_->out_data_event().Subscribe(
              [this](auto const& data) { OnSafeData(data); })} {
  Tie(*safe_stream_, *transport_);
}

#if AE_SUPPORT_ADAPTIVE_DATAGRAM
P2pSafeStream::P2pSafeStream(AeContext const& ae_context,
                             SafeStreamConfig config,
                             adaptive_datagram::Config datagram_config,
                             Ptr<Client> const& client, Uid destination,
                             P2pPortHandle handle)
    : P2pSafeStream{ae_context, AdaptiveTransportConfig(config),
                    std::make_shared<P2pAdaptiveDatagramStream>(
                        ae_context, client, std::move(destination),
                        std::move(handle), std::move(datagram_config))} {}
#endif
P2pSafeStream::~P2pSafeStream() = default;

WriteAction& P2pSafeStream::Write(DataBuffer&& data) {
  auto sized_data = sized_packet_gate_.WriteIn(std::move(data));
  return safe_stream_->Write(std::move(sized_data));
}

StreamInfo P2pSafeStream::stream_info() const {
  auto overhead = sized_packet_gate_.Overhead();
  auto info = safe_stream_->stream_info();
  if (info.max_element_size < overhead) {
    info.max_element_size = 0;
  } else {
    info.max_element_size -= overhead;
  }
  return info;
}

P2pSafeStream::StreamUpdateEvent const& P2pSafeStream::stream_update_event() {
  return safe_stream_->stream_update_event();
}

P2pSafeStream::OutDataEvent const& P2pSafeStream::out_data_event() {
  return out_data_event_;
}

void P2pSafeStream::Restream() { safe_stream_->Restream(); }

Event<void()> const& P2pSafeStream::receive_reset_event() {
  return receive_reset_event_;
}

void P2pSafeStream::OnSafeData(DataBuffer const& data) {
  auto const offset = safe_stream_->receive_session_offset();
  if (offset != receive_session_offset_) {
    receive_session_offset_ = offset;
    sized_packet_gate_.Reset();
    receive_reset_event_.Emit();
  }
  sized_packet_gate_.WriteOut(data);
}

}  // namespace ae
