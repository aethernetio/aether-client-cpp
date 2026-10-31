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

#ifndef AETHER_REGISTRATION_API_CLIENT_REG_API_UNSAFE_H_
#define AETHER_REGISTRATION_API_CLIENT_REG_API_UNSAFE_H_


#include "aether/config.h"
#if AE_SUPPORT_REGISTRATION

#include "aether/api_protocol/api_protocol.h"
#include "aether/crypto/icrypto_provider.h"
#include "aether/crypto/signed_key.h"
#include "aether/events/events.h"
#include "aether/types/server_id.h"
#include "aether/types/uid.h"

#include "aether/registration/api/client_reg_api_safe.h"
#include "aether/registration/api/global_reg_client_api.h"
#include "aether/registration/api/server_registration_api.h"
#include "aether/work_cloud_api/server_descriptor.h"
  #include "aether/work_cloud_api/client_api/client_api_unsafe.h"

namespace ae {


class ClientRegRootApi final : public DeclareApi<ClientRegRootApi> {
public:
  EventSystem& event_system_;
 public:
  explicit ClientRegRootApi(EventSystem& event_system,
                            IDecryptProvider& root_decrypt_provider,
                            IDecryptProvider& global_decrypt_provider);

  void Enter(SubApi<ClientRegApiSafe> sub_api);
  void EnterGlobal(SubApi<GlobalRegClientApi> sub_api);

  Event<void(SignedKey)> server_pub_key_event;
  Event<void(PowParams)> pow_params_event;
  Event<void(RegistrationDirectResult)> registration_direct_event;
  Event<void(std::vector<ServerDescriptor>)> resolved_servers_event;

  void SendAsymmetricPublicKey(SignedKey key);
  void SendPowParams(PowParams params);
  void SendRegistrationDirectResult(RegistrationDirectResult result);
  void SendResolvedServers(std::vector<ServerDescriptor> servers);

  API_LIST(METHOD(3, Enter), METHOD(4, EnterGlobal),
           METHOD(70, SendAsymmetricPublicKey), METHOD(67, SendPowParams),
           METHOD(66, SendRegistrationDirectResult), METHOD(68, SendResolvedServers))

 private:
  IDecryptProvider* root_decrypt_provider_;
  IDecryptProvider* global_decrypt_provider_;

  ClientRegApiSafe client_reg_api_;
  GlobalRegClientApi global_reg_client_api_;
};
}  // namespace ae
#endif

#endif  // AETHER_REGISTRATION_API_CLIENT_REG_API_UNSAFE_H_