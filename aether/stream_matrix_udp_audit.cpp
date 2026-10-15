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

#include "aether/stream_matrix_udp_audit.h"

namespace ae {
namespace {

StreamMatrixUdpPayloadAuditFn g_tx_audit = nullptr;
StreamMatrixUdpPayloadAuditFn g_rx_audit = nullptr;

}  // namespace

void SetStreamMatrixUdpPayloadAudit(StreamMatrixUdpPayloadAuditFn on_tx,
                                    StreamMatrixUdpPayloadAuditFn on_rx) {
  g_tx_audit = on_tx;
  g_rx_audit = on_rx;
}

void NotifyStreamMatrixUdpTxPayload(std::size_t payload_bytes) {
  if (g_tx_audit != nullptr) {
    g_tx_audit(payload_bytes);
  }
}

void NotifyStreamMatrixUdpRxPayload(std::size_t payload_bytes) {
  if (g_rx_audit != nullptr) {
    g_rx_audit(payload_bytes);
  }
}

}  // namespace ae
