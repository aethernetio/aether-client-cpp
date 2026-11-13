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

#ifndef AETHER_CLIENT_MESSAGES_P2P_ADAPTIVE_DATAGRAM_STREAM_H_
#define AETHER_CLIENT_MESSAGES_P2P_ADAPTIVE_DATAGRAM_STREAM_H_

#include <memory>

#include "aether/adaptive_datagram/adaptive_datagram_stream.h"
#include "aether/client_messages/p2p_message_stream.h"

namespace ae {

// Owns a dedicated P2pStream and adapts independent datagrams without retries
// or ordering. Both peers must use this protocol on their receive port. The
// local context/client must outlive the stream. Supply a known peer clock
// offset for one-way deadlines; a common clock is sufficient for two clients in
// one process. A linked TCP or mixed UDP/TCP send path is rejected, including
// after failover.
class P2pAdaptiveDatagramStream final : public ByteIStream {
 public:
  P2pAdaptiveDatagramStream(
      AeContext const& context, Ptr<Client> const& client, Uid destination,
      P2pPortHandle handle, adaptive_datagram::Config config,
      RequestPolicy::Variant send_policy = RequestPolicy::MainServer{});
  ~P2pAdaptiveDatagramStream() override;
  AE_CLASS_NO_COPY_MOVE(P2pAdaptiveDatagramStream);

  WriteAction& Write(DataBuffer&& data) override;
  StreamInfo stream_info() const override;
  StreamUpdateEvent const& stream_update_event() override;
  OutDataEvent const& out_data_event() override;
  void Restream() override;

  AdaptiveDatagramStream::DeliveryEvent const& delivery_event();
  adaptive_datagram::StreamStatistics statistics() const;
  adaptive_datagram::RateControllerMetrics const& metrics() const;
  std::uint64_t rate_bytes_per_second() const;
  bool valid() const;
  P2pStream::SendPathTransportView InspectSendPathTransport() const;
  static bool IsSupportedSendPath(P2pStream::SendPathTransportView const& path);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ae
#endif  // AETHER_CLIENT_MESSAGES_P2P_ADAPTIVE_DATAGRAM_STREAM_H_
