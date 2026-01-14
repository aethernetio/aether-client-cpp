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

#include "aether/stream_api/tied_gates.h"

namespace ae {
P2pSafeStream::P2pSafeStream(ActionContext action_context,
                             SafeStreamConfig const& config,
                             RcPtr<P2pStream> p2p_stream)
    : sized_packet_gate_{},
      safe_stream_{action_context, config},
      p2p_stream_{std::move(p2p_stream)},
      out_data_sub_{TiedEventOutData(
          [this](auto const& data) { out_data_event_.Emit(data); },
          sized_packet_gate_, safe_stream_)} {
  Tie(safe_stream_, *p2p_stream_);
}

ActionPtr<WriteAction> P2pSafeStream::Write(DataBuffer&& data) {
  auto sized_data = sized_packet_gate_.WriteIn(std::move(data));
  return safe_stream_.Write(std::move(sized_data));
}

StreamInfo P2pSafeStream::stream_info() const {
  auto overhead = sized_packet_gate_.Overhead();
  auto info = safe_stream_.stream_info();
  if (info.max_element_size < overhead) {
    info.max_element_size = 0;
  } else {
    info.max_element_size -= overhead;
  }
  return info;
}

P2pSafeStream::StreamUpdateEvent::Subscriber
P2pSafeStream::stream_update_event() {
  return safe_stream_.stream_update_event();
}

P2pSafeStream::OutDataEvent::Subscriber P2pSafeStream::out_data_event() {
  return EventSubscriber{out_data_event_};
}

void P2pSafeStream::Restream() { safe_stream_.Restream(); }

}  // namespace ae
