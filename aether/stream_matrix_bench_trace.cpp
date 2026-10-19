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

#include "aether/stream_matrix_bench_trace.h"

#include <atomic>

namespace ae {
namespace {

StreamMatrixSafeStreamSendTraceFn g_on_send = nullptr;
StreamMatrixWriteStatusTraceFn g_on_write_status = nullptr;
StreamMatrixSafeStreamAckTraceFn g_on_ack = nullptr;
StreamMatrixSafeStreamRecvTraceFn g_on_recv = nullptr;
thread_local char g_trace_side{'?'};
std::atomic<std::uint64_t> g_ss_correlation{1};

}  // namespace

void SetStreamMatrixTraceSide(char side) { g_trace_side = side; }

char StreamMatrixTraceSide() { return g_trace_side; }

std::uint64_t NextSafeStreamTraceCorrelationId() {
  return g_ss_correlation.fetch_add(1);
}

void SetStreamMatrixBenchTrace(StreamMatrixSafeStreamSendTraceFn on_send,
                               StreamMatrixWriteStatusTraceFn on_write_status,
                               StreamMatrixSafeStreamAckTraceFn on_ack,
                               StreamMatrixSafeStreamRecvTraceFn on_recv) {
  g_on_send = on_send;
  g_on_write_status = on_write_status;
  g_on_ack = on_ack;
  g_on_recv = on_recv;
}

void NotifyStreamMatrixSafeStreamSend(StreamMatrixSafeStreamSendTrace const& t) {
  if (g_on_send != nullptr) {
    g_on_send(t);
  }
}

void NotifyStreamMatrixSafeStreamAck(std::uint16_t ack_offset,
                                     StreamMatrixSafeStreamAckRole role) {
  if (g_on_ack != nullptr) {
    g_on_ack(StreamMatrixSafeStreamAckTrace{.side = g_trace_side,
                                            .role = role,
                                            .ack_offset = ack_offset});
  }
}

void NotifyStreamMatrixSafeStreamRecv(std::uint16_t begin_offset,
                                      std::uint16_t delta_offset,
                                      std::uint8_t repeat_count, bool reset,
                                      std::size_t data_bytes) {
  if (g_on_recv != nullptr) {
    g_on_recv(StreamMatrixSafeStreamRecvTrace{
        .side = g_trace_side,
        .begin_offset = begin_offset,
        .delta_offset = delta_offset,
        .repeat_count = repeat_count,
        .reset = reset,
        .data_bytes = data_bytes});
  }
}

void NotifyStreamMatrixWriteStatus(char side, std::size_t user_bytes,
                                   int write_status) {
  if (g_on_write_status != nullptr) {
    g_on_write_status(side, user_bytes, write_status);
  }
}

}  // namespace ae
