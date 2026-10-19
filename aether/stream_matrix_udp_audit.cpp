/*
 * Copyright 2026 Aethernet Inc.
 */

#include "aether/stream_matrix_udp_audit.h"

namespace ae {
namespace {

StreamMatrixUdpPayloadAuditFn g_tx_audit = nullptr;
StreamMatrixUdpPayloadAuditFn g_rx_audit = nullptr;
StreamMatrixUdpRecvErrorFn g_recv_error_audit = nullptr;
thread_local std::uint64_t g_audit_socket_id{};
thread_local char const* g_audit_endpoint = "";

StreamMatrixUdpPayloadEvent MakePayloadEvent(std::size_t payload_bytes) {
  return StreamMatrixUdpPayloadEvent{
      .payload_bytes = payload_bytes,
      .socket_id = g_audit_socket_id,
      .endpoint = g_audit_endpoint,
  };
}

StreamMatrixUdpRecvErrorEvent MakeRecvErrorEvent(int wsa_error,
                                                 std::size_t recv_buffer_bytes) {
  return StreamMatrixUdpRecvErrorEvent{
      .wsa_error = wsa_error,
      .recv_buffer_bytes = recv_buffer_bytes,
      .socket_id = g_audit_socket_id,
      .endpoint = g_audit_endpoint,
  };
}

}  // namespace

void SetStreamMatrixUdpPayloadAudit(StreamMatrixUdpPayloadAuditFn on_tx,
                                    StreamMatrixUdpPayloadAuditFn on_rx) {
  g_tx_audit = on_tx;
  g_rx_audit = on_rx;
}

void SetStreamMatrixUdpRecvErrorAudit(StreamMatrixUdpRecvErrorFn on_recv_error) {
  g_recv_error_audit = on_recv_error;
}

void SetStreamMatrixUdpAuditContext(std::uint64_t socket_id, char const* endpoint) {
  g_audit_socket_id = socket_id;
  g_audit_endpoint = endpoint != nullptr ? endpoint : "";
}

void NotifyStreamMatrixUdpTxPayload(std::size_t payload_bytes) {
  if (g_tx_audit != nullptr) {
    g_tx_audit(MakePayloadEvent(payload_bytes));
  }
}

void NotifyStreamMatrixUdpRxPayload(std::size_t payload_bytes) {
  if (g_rx_audit != nullptr) {
    g_rx_audit(MakePayloadEvent(payload_bytes));
  }
}

void NotifyStreamMatrixUdpRecvError(int wsa_error, std::size_t recv_buffer_bytes) {
  if (g_recv_error_audit != nullptr) {
    g_recv_error_audit(MakeRecvErrorEvent(wsa_error, recv_buffer_bytes));
  }
}

}  // namespace ae
