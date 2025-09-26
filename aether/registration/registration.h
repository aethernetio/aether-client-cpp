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

#  include "aether/common.h"
#  include "aether/memory.h"
#  include "aether/ptr/ptr.h"
#  include "aether/types/uid.h"
#  include "aether/ptr/ptr_view.h"
#  include "aether/actions/action.h"
#  include "aether/actions/action_ptr.h"
#  include "aether/stream_api/istream.h"
#  include "aether/types/state_machine.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/client.h"
#  include "aether/registration_cloud.h"

#  include "aether/methods/client_reg_api/client_reg_api.h"
#  include "aether/methods/client_reg_api/client_reg_root_api.h"
#  include "aether/methods/client_reg_api/client_global_reg_api.h"

#  include "aether/methods/server_reg_api/root_api.h"
#  include "aether/methods/server_reg_api/global_reg_server_api.h"
#  include "aether/methods/server_reg_api/server_registration_api.h"

#  include "aether/registration/root_server_select_stream.h"

namespace ae {

class Aether;

class Registration final : public Action<Registration> {
  enum class State : std::uint8_t {
    kInitConnection,
    kRestreamConnection,
    kGetKeys,
    kWaitKeys,
    kGetPowParams,
    kMakeRegistration,
    kRequestCloudResolving,
    kRegistered,
    kRegistrationFailed,
  };

 public:
  Registration(ActionContext action_context, ObjPtr<Aether> const& aether,
               RegistrationCloud::ptr const& reg_cloud, Uid parent_uid,
               Client::ptr client);
  ~Registration() override;

  UpdateStatus Update();

  Client::ptr client() const;

 private:
  void InitConnection();
  void Restream();
  void GetKeys();
  TimePoint WaitKeys();
  void RequestPowParams();
  void MakeRegistration();
  void ResolveCloud();
  void OnCloudResolved(std::vector<ServerDescriptor> const& servers);

  ActionContext action_context_;
  PtrView<Aether> aether_;
  Ptr<Client> client_;
  Uid parent_uid_;

  ProtocolContext protocol_context_;
  ClientRegRootApi client_root_api_;
  ClientApiRegSafe client_safe_api_;
  ClientGlobalRegApi client_global_reg_api_;
  RootApi server_reg_root_api_;
  ServerRegistrationApi server_reg_api_;
  GlobalRegServerApi global_reg_server_api_;

  RootServerSelectStream root_server_select_stream_;

  StateMachine<State> state_;

  Duration response_timeout_;
  TimePoint last_request_time_;

  Key server_pub_key_;
  Key master_key_;
  Uid uid_;
  Uid ephemeral_uid_;
  Key sign_pk_;
  PowParams pow_params_;
  Key aether_global_key_;
  std::vector<ServerId> client_cloud_;

  class RegistrationAsyncKeyProvider* server_async_key_provider_;
  class RegistrationSyncKeyProvider* server_sync_key_provider_;

  std::unique_ptr<ByteStream> reg_server_stream_;
  std::unique_ptr<ByteStream> global_reg_server_stream_;

  ActionPtr<StreamWriteAction> packet_write_action_;
  Subscription connection_subscription_;
  Subscription raw_transport_send_action_subscription_;
  Subscription reg_server_write_subscription_;
  Subscription response_sub_;
  Subscription state_change_subscription_;
};
}  // namespace ae

#endif
#endif  // AETHER_REGISTRATION_REGISTRATION_H_
