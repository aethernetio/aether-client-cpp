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

#ifndef AETHER_WORK_CLOUD_API_CLIENT_API_CLIENT_API_UNSAFE_H_
#define AETHER_WORK_CLOUD_API_CLIENT_API_CLIENT_API_UNSAFE_H_

#include "aether/api_protocol/api_protocol.h"

#include "aether/crypto/icrypto_provider.h"

#include "aether/work_cloud_api/client_api/client_api_safe.h"
#include "aether/events/events.h"
#include "aether/crypto/signed_key.h"
#include "aether/work_cloud_api/server_descriptor.h"
#include "aether/registration/api/server_registration_api.h"

namespace ae {


class ClientApiUnsafe : public DeclareApi<ClientApiUnsafe> {
 public:
  explicit ClientApiUnsafe(EventSystem& event_system,
                           IDecryptProvider& decrypt_provider);

  void SendSafeApiData(SubApi<ClientApiSafe> sub_api);

  Event<void(SignedKey)> server_pub_key_event;
  Event<void(PowParams)> pow_params_event;
  Event<void(std::vector<ServerDescriptor>)> resolved_servers_event;
  Event<void()> ping_ack_event;

  void SendAsymmetricPublicKey(SignedKey key);
  void SendPowParams(PowParams params);
  void SendResolvedServers(std::vector<ServerDescriptor> servers);
  void PingAck();

  API_LIST(METHOD(4, SendSafeApiData), METHOD(70, SendAsymmetricPublicKey),
           METHOD(67, SendPowParams), METHOD(68, SendResolvedServers),
           METHOD(24, PingAck))

  ClientApiSafe& client_api_safe() { return client_safe_api_; }

 private:
  IDecryptProvider* decrypt_provider_;
  ClientApiSafe client_safe_api_;
};
}  // namespace ae

#endif  // AETHER_WORK_CLOUD_API_CLIENT_API_CLIENT_API_UNSAFE_H_