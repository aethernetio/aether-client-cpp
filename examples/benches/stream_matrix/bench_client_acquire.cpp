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

#include "stream_matrix/bench_transport.h"

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

void WriteBenchTransportPolicy(std::ostream& out) {
  out << "bench_matrix_udp_modes=" << kBenchMatrixUdpEnabled
      << " bench_matrix_tcp_modes=" << kBenchMatrixTcpEnabled
      << " reg_bootstrap_may_use_tcp=" << kBenchRegBootstrapMayUseTcp << '\n';
  if (kBenchMatrixUdpEnabled && !kSdkTcpEnabled) {
    out << "reg_hypothesis: AE_SUPPORT_TCP=0 filters TCP registration endpoints "
           "(FilterProtocol); not proven alone but breaks typical bootstrap\n";
  }
  if (kBenchMatrixUdpEnabled && !kSdkTcpEnabled) {
    out << "bench_policy: UDP-only SDK; clients must load from persistent "
           "state/ prepared by aether_stream_matrix_bootstrap (TCP process)\n";
  }
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

void WriteClientAcquireFailureHints(
    std::ostream& out, ClientSelectOutcome const& select_a,
    ClientSelectOutcome const& select_b) {
  if (!select_a.finished || select_a.ok) {
    if (!select_b.finished || select_b.ok) {
      return;
    }
  }
  out << "client_acquire_hints: failure before matrix; check registration "
         "(GetKeys), endpoint/protocol filter, server connection, auth\n";
  if (select_a.error_code == 1 || select_b.error_code == 1) {
    out << "client_acquire_hints: SelectClient error_code=1 => registration "
           "cloud/root-server path failed (see tele SERVER_CONNECTION_ERROR / "
           "RegisterWaitKeysTimeout)\n";
  }
  WriteBenchTransportPolicy(out);
}

}  // namespace ae::stream_matrix_bench
