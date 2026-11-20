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

#include <optional>

#include "aether/ae_context.h"
#include "aether/common.h"
#include "aether/config.h"
#include "aether/memory.h"

#if AE_SUPPORT_ADAPTIVE_DATAGRAM
#  include "aether/adaptive_datagram/adaptive_datagram_config.h"
#  include "aether/client_messages/p2p_port_handle.h"
#endif
#include "aether/safe_stream/safe_stream_config.h"
#include "aether/stream_api/istream.h"
#include "aether/stream_api/sized_packet_gate.h"
#include "aether/types/uid.h"

namespace ae {
class Client;
class P2pSafeStream final : public ByteIStream {
 public:
  P2pSafeStream(AeContext const& ae_context, SafeStreamConfig const& config,
                std::shared_ptr<ByteIStream> transport);
#if AE_SUPPORT_ADAPTIVE_DATAGRAM
  // Use one adaptive UDP route. Both peers must construct the same stack and
  // configure reciprocal adaptive session IDs and the peer clock offset.
  // The datagram config controls pacing and per-attempt lifetimes. SafeStream
  // confirms complete messages and retries missing byte ranges.
  P2pSafeStream(AeContext const& ae_context, SafeStreamConfig config,
                adaptive_datagram::Config datagram_config,
                Ptr<Client> const& client, Uid destination,
                P2pPortHandle handle);
#endif
  ~P2pSafeStream() override;

  AE_CLASS_NO_COPY_MOVE(P2pSafeStream)

  WriteAction& Write(DataBuffer&& data) override;
  StreamInfo stream_info() const override;
  StreamUpdateEvent const& stream_update_event() override;
  OutDataEvent const& out_data_event() override;
  // Emitted before data from a new receive session reaches packet consumers.
  Event<void()> const& receive_reset_event();
  void Restream() override;

 private:
  void OnSafeData(DataBuffer const& data);

  SizedPacketGate sized_packet_gate_;
  std::shared_ptr<ByteIStream> transport_;
  std::unique_ptr<SafeStream> safe_stream_;
  OutDataEvent out_data_event_;
  Event<void()> receive_reset_event_;
  std::optional<std::size_t> receive_session_offset_;
  std::array<Subscription, 2> out_data_sub_;
};

}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_P2P_SAFE_MESSAGE_STREAM_H_
