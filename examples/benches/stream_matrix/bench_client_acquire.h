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

#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_CLIENT_ACQUIRE_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_CLIENT_ACQUIRE_H_

#include <iosfwd>
#include <optional>

#include "aether/client.h"

namespace ae::stream_matrix_bench {

struct ClientSelectOutcome {
  bool finished{false};
  bool ok{false};
  int error_code{0};
};

void WriteSdkTransportCompileFlags(std::ostream& out);
void WriteClientSelectDiagnostics(std::ostream& out, char const* role,
                                  ClientSelectOutcome const& select,
                                  Client::ptr const& client);

}  // namespace ae::stream_matrix_bench

#endif
