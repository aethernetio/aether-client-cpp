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

#ifndef AETHER_AE_ACTIONS_REGISTRATION_REGISTRATION_H_
#define AETHER_AE_ACTIONS_REGISTRATION_REGISTRATION_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION

#  include <vector>

#  include "aether/uid.h"
#  include "aether/common.h"
#  include "aether/memory.h"
#  include "aether/ptr/ptr.h"
#  include "aether/ptr/ptr_view.h"
#  include "aether/state_machine.h"
#  include "aether/actions/action.h"
#  include "aether/actions/action_view.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/client.h"

#  include "aether/stream_api/istream.h"
#  include "aether/crypto/ikey_provider.h"

#  include "aether/server_list/server_list.h"
#  include "aether/stream_api/delegate_gate.h"
#  include "aether/transport/server/server_channel_stream.h"

#  include "aether/methods/client_reg_api/client_reg_api.h"
#  include "aether/methods/client_reg_api/client_reg_root_api.h"
#  include "aether/methods/client_reg_api/client_global_reg_api.h"

#  include "aether/methods/server_reg_api/root_api.h"
#  include "aether/methods/server_reg_api/global_reg_server_api.h"
#  include "aether/methods/server_reg_api/server_registration_api.h"

namespace ae {

class Aether;

class Registration : public Action<Registration> {
  enum class State : std::uint8_t {
    kSelectConnection,
    kWaitingConnection,
    kConnected,
    kGetKeys,
    kWaitKeys,
    kGetPowParams,
    kMakeRegistration,
    kRequestCloudResolving,
    kRegistered,
    kRegistrationFailed,
  };

 public:
  Registration(ActionContext action_context, PtrView<Aether> aether,
               Uid parent_uid, Client::ptr client);
  ~Registration() override;

  TimePoint Update(TimePoint current_time) override;

  Client::ptr client() const;

 private:
  void IterateConnection();
  void IterateChannel();

  void Connected(TimePoint current_time);

  void GetKeys(TimePoint current_time);
  TimePoint WaitKeys(TimePoint current_time);
  void RequestPowParams(TimePoint current_time);
  void MakeRegistration(TimePoint current_time);
  void ResolveCloud(TimePoint current_time);
  void OnCloudResolved(std::vector<ServerDescriptor> const& servers);

  std::unique_ptr<ByteStream> CreateRegServerStream(
      std::unique_ptr<IAsyncKeyProvider> async_key_provider,
      std::unique_ptr<ISyncKeyProvider> sync_key_provider);
  std::unique_ptr<ByteStream> CreateGlobalRegServerStream(
      std::unique_ptr<IAsyncKeyProvider> global_async_key_provider,
      std::unique_ptr<ISyncKeyProvider> global_sync_key_provider,
      DelegateWriteInGate<DataBuffer> enter_global_api_gate);

  PtrView<Aether> aether_;
  Ptr<Client> client_;
  Uid parent_uid_;

  std::unique_ptr<ServerList> server_list_;
  std::optional<AsyncForLoop<std::unique_ptr<ServerChannelStream>>>
      connection_selection_;
  std::unique_ptr<ServerChannelStream> server_channel_stream_;

  ProtocolContext protocol_context_;
  ClientRegRootApi client_root_api_;
  ClientApiRegSafe client_safe_api_;
  ClientGlobalRegApi client_global_reg_api_;
  RootApi server_reg_root_api_;
  ServerRegistrationApi server_reg_api_;
  GlobalRegServerApi global_reg_server_api_;

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
  std::vector<ServerId> cloud_;

  class RegistrationAsyncKeyProvider* server_async_key_provider_;
  class RegistrationSyncKeyProvider* server_sync_key_provider_;

  std::unique_ptr<ByteStream> reg_server_stream_;
  std::unique_ptr<ByteStream> global_reg_server_stream_;

  ActionView<StreamWriteAction> packet_write_action_;
  Subscription connection_subscription_;
  Subscription raw_transport_send_action_subscription_;
  Subscription reg_server_write_subscription_;
  MultiSubscription subscriptions_;
  Subscription state_change_subscription_;
};
}  // namespace ae

#endif
#endif  // AETHER_AE_ACTIONS_REGISTRATION_REGISTRATION_H_
