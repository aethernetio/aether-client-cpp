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

#include "aether/tele.h"

namespace ae {
ClientApiUnsafe::ClientApiUnsafe(EventSystem& event_system,
                                 IDecryptProvider& decrypt_provider)

    : server_pub_key_event{event_system},
      pow_params_event{event_system},
      resolved_servers_event{event_system},
      ping_ack_event{event_system},
      decrypt_provider_{&decrypt_provider},
      client_safe_api_{event_system} {}


void ClientApiUnsafe::SendSafeApiData(SubApi<ClientApiSafe> sub_api) {
  auto unsafe_data = decrypt_provider_->Decrypt(std::move(sub_api).buffer());
  AE_TELED_DEBUG("Client api unsafe data {}", unsafe_data);

  auto parser = ApiParser{server_context().protocol_context(), unsafe_data};
  // not fully parsed
  if (!parser.Parse(client_safe_api_)) {
    server_context().reader().Cancel();
  }
}

void ClientApiUnsafe::SendAsymmetricPublicKey(SignedKey key) {
  server_pub_key_event.Emit(std::move(key));
}

void ClientApiUnsafe::SendPowParams(PowParams params) {
  pow_params_event.Emit(std::move(params));
}

void ClientApiUnsafe::SendResolvedServers(std::vector<ServerDescriptor> servers) {
  resolved_servers_event.Emit(std::move(servers));
}

void ClientApiUnsafe::PingAck() {
  ping_ack_event.Emit();
}

}  // namespace ae