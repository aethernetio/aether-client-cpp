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

#include "stream_matrix/bench_transport.h"

#include "aether/client_messages/p2p_message_stream.h"

namespace ae::stream_matrix_bench {

bool ModeCompiledIn(BenchTransportKind transport) {
  switch (transport) {
    case BenchTransportKind::kUdp:
      return kSdkUdpEnabled;
    case BenchTransportKind::kTcp:
      return kSdkTcpEnabled;
  }
  return false;
}

TransportVerifyResult VerifyP2pTransport(StreamInfo const& p2p_info,
                                         BenchTransportKind want) {
  if (!ModeCompiledIn(want)) {
    return TransportVerifyResult::kNotCompiledIn;
  }
  if (p2p_info.link_state != LinkState::kLinked) {
    return TransportVerifyResult::kUnlinked;
  }
  bool const is_tcp = p2p_info.is_reliable;
  switch (want) {
    case BenchTransportKind::kUdp:
      return is_tcp ? TransportVerifyResult::kWrongProtocol
                    : TransportVerifyResult::kOk;
    case BenchTransportKind::kTcp:
      return is_tcp ? TransportVerifyResult::kOk
                    : TransportVerifyResult::kWrongProtocol;
  }
  return TransportVerifyResult::kWrongProtocol;
}

static TransportVerifyResult VerifySendPathView(
    P2pStream::SendPathTransportView const& view, BenchTransportKind want) {
  auto agg = VerifyP2pTransport(view.aggregated, want);
  if (agg != TransportVerifyResult::kOk) {
    return agg;
  }
  auto pri = VerifyP2pTransport(view.at_priority0, want);
  if (pri != TransportVerifyResult::kOk) {
    return pri;
  }
  if (view.linked_udp_endpoint && view.linked_tcp_endpoint) {
    return TransportVerifyResult::kWrongProtocol;
  }
  switch (want) {
    case BenchTransportKind::kUdp:
      if (view.linked_tcp_endpoint) {
        return TransportVerifyResult::kWrongProtocol;
      }
      break;
    case BenchTransportKind::kTcp:
      if (view.linked_udp_endpoint) {
        return TransportVerifyResult::kWrongProtocol;
      }
      break;
  }
  return TransportVerifyResult::kOk;
}

TransportVerifyResult VerifyP2pStreamTransport(P2pStream const& stream,
                                               BenchTransportKind want) {
  if (!ModeCompiledIn(want)) {
    return TransportVerifyResult::kNotCompiledIn;
  }
  return VerifySendPathView(stream.InspectSendPathTransport(), want);
}

}  // namespace ae::stream_matrix_bench
