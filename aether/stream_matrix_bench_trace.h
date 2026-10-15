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

#ifndef AETHER_STREAM_MATRIX_BENCH_TRACE_H_
#define AETHER_STREAM_MATRIX_BENCH_TRACE_H_

#include <cstddef>
#include <cstdint>

namespace ae {

struct StreamMatrixSafeStreamSendTrace {
  char side;
  std::uint16_t begin_offset;
  std::uint16_t delta_offset;
  std::uint8_t repeat_count;
  bool reset;
  std::size_t data_bytes;
};

using StreamMatrixSafeStreamSendTraceFn =
    void (*)(StreamMatrixSafeStreamSendTrace const&);

using StreamMatrixWriteStatusTraceFn = void (*)(char side,
                                                std::size_t user_bytes,
                                                int write_status);

void SetStreamMatrixBenchTrace(StreamMatrixSafeStreamSendTraceFn on_send,
                               StreamMatrixWriteStatusTraceFn on_write_status);

void NotifyStreamMatrixSafeStreamSend(StreamMatrixSafeStreamSendTrace const& t);
void NotifyStreamMatrixWriteStatus(char side, std::size_t user_bytes,
                                   int write_status);

}  // namespace ae

#endif
