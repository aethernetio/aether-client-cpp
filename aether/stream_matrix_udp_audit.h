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

#ifndef AETHER_STREAM_MATRIX_UDP_AUDIT_H_
#define AETHER_STREAM_MATRIX_UDP_AUDIT_H_

#include <cstddef>
#include <cstdint>

namespace ae {

struct StreamMatrixUdpPayloadEvent {
  std::size_t payload_bytes{};
  std::uint64_t socket_id{};
  char const* endpoint{};  // valid for the duration of the callback only
};

struct StreamMatrixUdpRecvErrorEvent {
  int wsa_error{};
  std::size_t recv_buffer_bytes{};
  std::uint64_t socket_id{};
  char const* endpoint{};
};

using StreamMatrixUdpPayloadAuditFn = void (*)(StreamMatrixUdpPayloadEvent const&);
using StreamMatrixUdpRecvErrorFn = void (*)(StreamMatrixUdpRecvErrorEvent const&);

void SetStreamMatrixUdpPayloadAudit(StreamMatrixUdpPayloadAuditFn on_tx,
                                    StreamMatrixUdpPayloadAuditFn on_rx);

void SetStreamMatrixUdpRecvErrorAudit(StreamMatrixUdpRecvErrorFn on_recv_error);

void SetStreamMatrixUdpAuditContext(std::uint64_t socket_id, char const* endpoint);

void NotifyStreamMatrixUdpTxPayload(std::size_t payload_bytes);
void NotifyStreamMatrixUdpRxPayload(std::size_t payload_bytes);
void NotifyStreamMatrixUdpRecvError(int wsa_error, std::size_t recv_buffer_bytes);

}  // namespace ae

#endif
