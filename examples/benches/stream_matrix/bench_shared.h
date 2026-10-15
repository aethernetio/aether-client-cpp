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

#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_SHARED_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_SHARED_H_

#include "aether/types/uid.h"

namespace ae::stream_matrix_bench {

inline Uid const& StreamMatrixParentUid() {
  static Uid const kUid =
      Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");
  return kUid;
}

inline constexpr char kStreamMatrixSenderId[] = "sender";
inline constexpr char kStreamMatrixReceiverId[] = "receiver";

}  // namespace ae::stream_matrix_bench

#endif
