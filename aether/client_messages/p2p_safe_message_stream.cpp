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

namespace ae {
P2pSafeStream::P2pSafeStream(ActionContext action_context,
                             SafeStreamConfig const& config,
                             std::unique_ptr<P2pStream> base_stream)
    : sized_packet_gate_{},
      safe_stream_{action_context, config},
      base_stream_{std::move(base_stream)} {
  Tie(sized_packet_gate_, safe_stream_, *base_stream_);
}

P2pSafeStream::InGate& P2pSafeStream::in() { return sized_packet_gate_; }

void P2pSafeStream::LinkOut(OutGate& /* out */) { assert(false); }

}  // namespace ae
