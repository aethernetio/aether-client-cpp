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

#include "stream_matrix/bench_client_acquire.h"

#include "aether/config.h"

namespace ae::stream_matrix_bench {
namespace {

char const* SelectErrorStage(int code) {
  switch (code) {
    case 1:
      return "registration";
    case 2:
      return "client_not_found_or_registration_disabled";
    default:
      return "unknown";
  }
}

}  // namespace

void WriteSdkTransportCompileFlags(std::ostream& out) {
  out << "sdk_compile AE_SUPPORT_UDP=" << AE_SUPPORT_UDP
      << " AE_SUPPORT_TCP=" << AE_SUPPORT_TCP
      << " AE_SUPPORT_REGISTRATION=" << AE_SUPPORT_REGISTRATION << '\n';
}

void WriteClientSelectDiagnostics(std::ostream& out, char const* role,
                                  ClientSelectOutcome const& select,
                                  Client::ptr const& client) {
  out << "client_acquire role=" << role;
  if (client.is_valid()) {
    auto loaded = client.Load();
    out << " status=ok client_id=" << loaded->id()
        << " has_uid=" << (!loaded->uid().empty() ? "yes" : "no");
    out << " load_state=loaded";
  } else if (select.finished && !select.ok) {
    out << " status=failed stage=select_client_result"
        << " error_code=" << select.error_code
        << " failure_stage=" << SelectErrorStage(select.error_code);
  } else if (!select.finished) {
    out << " status=timeout stage=select_client_pending"
        << " (load_state/registration/endpoint/connect/auth not reached)";
  } else {
    out << " status=failed stage=unknown";
  }
  out << '\n';
  WriteSdkTransportCompileFlags(out);
}

}  // namespace ae::stream_matrix_bench
