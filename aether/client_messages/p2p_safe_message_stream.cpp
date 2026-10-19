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

#include <utility>

#include "aether/safe_stream/safe_stream.h"
#include "aether/stream_api/tied_gates.h"

namespace ae {
P2pSafeStream::P2pSafeStream(AeContext const& ae_context,
                             SafeStreamConfig const& config,
                             std::shared_ptr<ByteIStream> p2p_stream)
    : ae_context_{ae_context},
      sized_packet_gate_{ae_context},
      safe_stream_{std::make_unique<SafeStreamImpl>(ae_context, config)},
      p2p_stream_{std::move(p2p_stream)},
      out_data_event_{ae_context},
      out_data_sub_{TiedEventOutData(
          [this](auto const& data) { out_data_event_.Emit(data); },
          sized_packet_gate_, *safe_stream_)} {
  Tie(*safe_stream_, *p2p_stream_);
}

P2pSafeStream::~P2pSafeStream() = default;

WriteAction& P2pSafeStream::Write(DataBuffer&& data) {
  auto offer = TryWrite(data);
  if (offer.result == P2pWriteOfferResult::kAccepted && offer.action != nullptr) {
    return *offer.action;
  }
  if (!failed_write_ || failed_write_->is_finished()) {
    failed_write_.emplace(ae_context_);
  }
  return *failed_write_;
}

P2pWriteOffer P2pSafeStream::TryWrite(DataBuffer const& data) {
  auto const info = stream_info();
  if (info.link_state == LinkState::kLinkError) {
    return {P2pWriteOfferResult::kError, nullptr};
  }
  if (data.size() > info.max_element_size) {
    return {P2pWriteOfferResult::kError, nullptr};
  }
  auto const framed_size = sized_packet_gate_.FramedSizeForPayload(data.size());
  if (!safe_stream_->CanAcceptSendBytes(framed_size)) {
    return {P2pWriteOfferResult::kWouldBlock, nullptr};
  }
  DataBuffer sized_data = sized_packet_gate_.WriteIn(DataBuffer{data});
  auto& wa = safe_stream_->Write(std::move(sized_data));
  if (dynamic_cast<FailedWriteAction*>(&wa) != nullptr) {
    return {P2pWriteOfferResult::kError, &wa};
  }
  return {P2pWriteOfferResult::kAccepted, &wa};
}

bool P2pSafeStream::CanWritePayload(std::size_t payload_bytes) const {
  auto const info = stream_info();
  if (info.link_state != LinkState::kLinked) {
    return false;
  }
  if (payload_bytes > info.max_element_size) {
    return false;
  }
  return safe_stream_->CanAcceptSendBytes(
      sized_packet_gate_.FramedSizeForPayload(payload_bytes));
}

std::size_t P2pSafeStream::send_buffer_free_bytes() const {
  return safe_stream_->send_buffer_free_bytes();
}

std::size_t P2pSafeStream::send_buffer_used_bytes() const {
  return safe_stream_->send_buffer_used_bytes();
}

Event<void()> const& P2pSafeStream::write_capacity_event() const {
  return safe_stream_->write_capacity_event();
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

}  // namespace ae
