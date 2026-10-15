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

namespace ae {
namespace {

StreamMatrixSafeStreamSendTraceFn g_on_send = nullptr;
StreamMatrixWriteStatusTraceFn g_on_write_status = nullptr;

}  // namespace

void SetStreamMatrixBenchTrace(StreamMatrixSafeStreamSendTraceFn on_send,
                               StreamMatrixWriteStatusTraceFn on_write_status) {
  g_on_send = on_send;
  g_on_write_status = on_write_status;
}

void NotifyStreamMatrixSafeStreamSend(StreamMatrixSafeStreamSendTrace const& t) {
  if (g_on_send != nullptr) {
    g_on_send(t);
  }
}

void NotifyStreamMatrixWriteStatus(char side, std::size_t user_bytes,
                                   int write_status) {
  if (g_on_write_status != nullptr) {
    g_on_write_status(side, user_bytes, write_status);
  }
}

}  // namespace ae
