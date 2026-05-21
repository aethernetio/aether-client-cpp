/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef AETHER_REGISTRATION_REGISTRATION_H_
#define AETHER_REGISTRATION_REGISTRATION_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION

#  include <vector>
#  include <optional>

#  include "aether/types/uid.h"
#  include "aether/crypto/key.h"
#  include "aether/events/events.h"
#  include "aether/types/server_id.h"
#  include "aether/types/client_config.h"
#  include "aether/executors/executors.h"
#  include "aether/types/small_function.h"

#  include "aether/registration_cloud.h"
#  include "aether/registration/api/client_reg_api_unsafe.h"
#  include "aether/registration/api/registration_root_api.h"
#  include "aether/registration/root_server_select_stream.h"
#  include "aether/registration/registration_crypto_provider.h"

namespace ae {
class Registration : Action {
 public:
  using RegistrationEvent = Event<void(Result<ClientConfig, int>)>;

  Registration(AeContext const& ae_context,
               Ptr<RegistrationCloud> const& reg_cloud, Uid parent_uid);
  ~Registration() override;

  RegistrationEvent::Subscriber registration();

 private:
  void InitConnection();
  void Restream();
  void Run();
  auto GetKeys();
  auto RequestPowParams();
  auto MakeRegistration();
  auto ResolveCloud();
  ClientConfig OnCloudResolved(std::vector<ServerDescriptor> const& servers);

  AeContext ae_context_;
  Uid parent_uid_;

  ProtocolContext protocol_context_;
  RegistrationCryptoProvider root_crypto_provider_;
  RegistrationCryptoProvider global_crypto_provider_;
  ClientRegRootApi client_root_api_;
  RegistrationRootApi server_reg_root_api_;
  RootServerSelectStream root_server_select_stream_;

  Duration response_timeout_;

  Key server_pub_key_;
  Key sign_pk_;
  PowParams pow_params_;
  Key aether_global_key_;
  std::vector<ServerId> client_cloud_;
  Key master_key_;
  Uid client_uid_;
  Uid ephemeral_uid_;

  std::optional<ex::AnyWaiter<ex::set_value_t(ClientConfig),
                              ex::set_error_t(int), ex::set_stopped_t()>>
      waiter_;
  RegistrationEvent registration_event_;
};
}  // namespace ae

#endif
#endif  // AETHER_REGISTRATION_REGISTRATION_H_
