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

#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_TRANSPORT_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_TRANSPORT_H_

#include "aether/config.h"
#include "aether/stream_api/istream.h"

#include "stream_matrix/bench_limits.h"

namespace ae {
class P2pStream;
}

namespace ae::stream_matrix_bench {

inline constexpr bool kSdkUdpEnabled = AE_SUPPORT_UDP != 0;
inline constexpr bool kSdkTcpEnabled = AE_SUPPORT_TCP != 0;

enum class TransportVerifyResult {
  kOk,
  kUnlinked,
  kWrongProtocol,
  kNotCompiledIn,
};

TransportVerifyResult VerifyP2pTransport(StreamInfo const& p2p_info,
                                         BenchTransportKind want);

TransportVerifyResult VerifyP2pStreamTransport(P2pStream const& stream,
                                               BenchTransportKind want);

bool ModeCompiledIn(BenchTransportKind transport);

}  // namespace ae::stream_matrix_bench

#endif
