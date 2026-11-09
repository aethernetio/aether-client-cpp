/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/work_cloud_api/client_api/client_api_unsafe.h"

#include "aether/diagnostics/bench_connection_event_diag.h"
#include "aether/types/data_buffer.h"

#include "aether/tele.h"

namespace ae {

ClientApiUnsafe::ClientApiUnsafe(EventSystem& event_system,
                                 IDecryptProvider& decrypt_provider)
    : server_pub_key_event{event_system},
      pow_params_event{event_system},
      resolved_servers_event{event_system},
      registration_direct_event{event_system},
      my_ip_event{event_system},
      decrypt_provider_{&decrypt_provider},
      client_safe_api_{event_system} {}

void ClientApiUnsafe::SendSafeApiData(SubApi<ClientApiSafe> sub_api) {
  DataBuffer enc_buffer = std::move(sub_api).buffer();
  std::uint32_t const enc_size = static_cast<std::uint32_t>(enc_buffer.size());
#if AE_BENCH_CONNECTION_EVENT_DIAG
  AE_BENCH_CONN_DIAG(
      "unsafe_send_safe_api_enter",
      bench_conn_event_diag::CurrentInboundMessageServerId(),
      bench_conn_event_diag::CurrentInboundClientUidLo(),
      bench_conn_event_diag::CurrentInboundClientConnInstance(), 0, enc_size, 0,
      -1, 0, -1);
#else
  (void)enc_size;
#endif
  auto unsafe_data = decrypt_provider_->Decrypt(std::move(enc_buffer));
  AE_TELED_DEBUG("Client api unsafe data {}", unsafe_data);

  std::uint32_t const plain_size = static_cast<std::uint32_t>(unsafe_data.size());
#if AE_BENCH_CONNECTION_EVENT_DIAG
  int const decrypt_ok = plain_size > 0 ? 1 : 0;
  AE_BENCH_CONN_DIAG(
      "unsafe_send_safe_api_decrypt",
      bench_conn_event_diag::CurrentInboundMessageServerId(),
      bench_conn_event_diag::CurrentInboundClientUidLo(),
      bench_conn_event_diag::CurrentInboundClientConnInstance(), 0, enc_size,
      plain_size, decrypt_ok, 0, -1);
#else
  (void)plain_size;
#endif

  auto parser = ApiParser{server_context().protocol_context(), unsafe_data};
  bool const inner_ok = parser.Parse(client_safe_api_);
#if AE_BENCH_CONNECTION_EVENT_DIAG
  AE_BENCH_CONN_DIAG(
      "unsafe_send_safe_api_inner_parse",
      bench_conn_event_diag::CurrentInboundMessageServerId(),
      bench_conn_event_diag::CurrentInboundClientUidLo(),
      bench_conn_event_diag::CurrentInboundClientConnInstance(), plain_size, 0,
      0, inner_ok ? 1 : 0, inner_ok ? 0 : 1, -1);
#endif
  if (!inner_ok) {
    server_context().reader().Cancel();
  }
}

void ClientApiUnsafe::SendAsymmetricPublicKey(SignedKey key) {
#if AE_BENCH_CONNECTION_EVENT_DIAG
  AE_BENCH_CONN_DIAG(
      "client_api_unsafe_handler",
      bench_conn_event_diag::CurrentInboundMessageServerId(),
      bench_conn_event_diag::CurrentInboundClientUidLo(),
      bench_conn_event_diag::CurrentInboundClientConnInstance(), 0, 0, 0, -1,
      70, -1);
#endif
  server_pub_key_event.Emit(std::move(key));
}

void ClientApiUnsafe::SendPowParams(PowParams params) {
#if AE_BENCH_CONNECTION_EVENT_DIAG
  AE_BENCH_CONN_DIAG(
      "client_api_unsafe_handler",
      bench_conn_event_diag::CurrentInboundMessageServerId(),
      bench_conn_event_diag::CurrentInboundClientUidLo(),
      bench_conn_event_diag::CurrentInboundClientConnInstance(), 0, 0, 0, -1,
      67, -1);
#endif
  pow_params_event.Emit(std::move(params));
}

void ClientApiUnsafe::SendResolvedServers(std::vector<ServerDescriptor> servers) {
#if AE_BENCH_CONNECTION_EVENT_DIAG
  AE_BENCH_CONN_DIAG(
      "client_api_unsafe_handler",
      bench_conn_event_diag::CurrentInboundMessageServerId(),
      bench_conn_event_diag::CurrentInboundClientUidLo(),
      bench_conn_event_diag::CurrentInboundClientConnInstance(), 0, 0, 0, -1,
      68, -1);
#endif
  resolved_servers_event.Emit(std::move(servers));
}

void ClientApiUnsafe::SendRegistrationDirect(RegistrationDirectResult result) {
#if AE_BENCH_CONNECTION_EVENT_DIAG
  AE_BENCH_CONN_DIAG(
      "client_api_unsafe_handler",
      bench_conn_event_diag::CurrentInboundMessageServerId(),
      bench_conn_event_diag::CurrentInboundClientUidLo(),
      bench_conn_event_diag::CurrentInboundClientConnInstance(), 0, 0, 0, -1,
      66, -1);
#endif
  registration_direct_event.Emit(std::move(result));
}

void ClientApiUnsafe::SendMyIpInfo(InfoIp info) {
#if AE_BENCH_CONNECTION_EVENT_DIAG
  AE_BENCH_CONN_DIAG(
      "client_api_unsafe_handler",
      bench_conn_event_diag::CurrentInboundMessageServerId(),
      bench_conn_event_diag::CurrentInboundClientUidLo(),
      bench_conn_event_diag::CurrentInboundClientConnInstance(), 0, 0, 0, -1,
      47, -1);
#endif
  my_ip_event.Emit(std::move(info));
}

}  // namespace ae
