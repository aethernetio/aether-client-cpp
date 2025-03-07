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

#ifndef AETHER_CLIENT_MESSAGES_P2P_SAFE_MESSAGE_STREAM_H_
#define AETHER_CLIENT_MESSAGES_P2P_SAFE_MESSAGE_STREAM_H_

#include "aether/common.h"
#include "aether/memory.h"
#include "aether/actions/action_context.h"

#include "aether/stream_api/istream.h"
#include "aether/stream_api/safe_stream.h"
#include "aether/stream_api/sized_packet_stream.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"

#include "aether/client_messages/p2p_message_stream.h"

namespace ae {

class P2pSafeStream final : public ByteStream {
 public:
  P2pSafeStream(ActionContext action_context, SafeStreamConfig const& config,
                std::unique_ptr<P2pStream> base_stream);

  AE_CLASS_NO_COPY_MOVE(P2pSafeStream)

  InGate& in() override;
  void LinkOut(OutGate& out) override;

 private:
  SizedPacketGate sized_packet_gate_;
  SafeStream safe_stream_;
  std::unique_ptr<P2pStream> base_stream_;
};

}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_P2P_SAFE_MESSAGE_STREAM_H_
