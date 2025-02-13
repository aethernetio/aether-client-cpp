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

#include "aether/ae_actions/registration/registration.h"

#if AE_SUPPORT_REGISTRATION

#  include <utility>

#  include "aether/crypto/crypto_definitions.h"
#  include "aether/crypto/key_gen.h"
#  include "aether/crypto/sign.h"
#  include "aether/aether.h"
#  include "aether/common.h"
#  include "aether/obj/domain.h"

#  include "aether/stream_api/stream_api.h"
#  include "aether/stream_api/crypto_stream.h"
#  include "aether/stream_api/protocol_stream.h"
#  include "aether/stream_api/tied_stream.h"

#  include "aether/crypto/async_crypto_provider.h"
#  include "aether/crypto/sync_crypto_provider.h"

#  include "aether/methods/server_reg_api/root_api.h"
#  include "aether/methods/server_reg_api/global_reg_server_api.h"
#  include "aether/methods/client_reg_api/client_global_reg_api.h"
#  include "aether/methods/server_reg_api/server_registration_api.h"

#  include "aether/server_list/no_filter_server_list_policy.h"

#  include "aether/ae_actions/registration/registration_key_provider.h"

#  include "aether/proof_of_work.h"

#  include "aether/tele/tele.h"

namespace ae {

Registration::Registration(ActionContext action_context, PtrView<Aether> aether,
                           Uid parent_uid, Client::ptr client)
    : Action{action_context},
      aether_{std::move(aether)},
      client_{std::move(client)},
      parent_uid_{std::move(parent_uid)},
      state_{State::kSelectConnection},
      // TODO: add configuration
      response_timeout_{std::chrono::seconds(20)},
      sign_pk_{aether_.Lock()->crypto->signs_pk_[kDefaultSignatureMethod]} {
  AE_TELE_INFO("Registration Started");

  auto aether_ptr = aether_.Lock();
  assert(aether_ptr);

  auto& cloud = aether_ptr->registration_cloud;
  if (!cloud) {
    aether_ptr->domain_->LoadRoot(cloud);
    assert(cloud);
  }

  auto adapter = cloud->adapter();
  if (!adapter) {
    aether_ptr->domain_->LoadRoot(adapter);
  }

  server_list_ =
      MakePtr<ServerList>(MakePtr<NoFilterServerListPolicy>(), cloud);
  connection_selection_ =
      MakePtr<AsyncForLoop>(AsyncForLoop<Ptr<ServerChannelStream>>::Construct(
          *server_list_, [this, aether_ptr, adapter{adapter}]() {
            auto item = server_list_->Get();
            return MakePtr<ServerChannelStream>(aether_ptr, adapter,
                                                item.server(), item.channel());
          }));

  // trigger action on state change
  state_change_subscription_ =
      state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });

  subscriptions_.Push(
      protocol_context_.MessageEvent<ClientApiRegSafe::GetKeysResponse>()
          .Subscribe(*this, MethodPtr<&Registration::OnGetKeysResponse>{}),
      protocol_context_.MessageEvent<ClientApiRegSafe::ResponseWorkProofData>()
          .Subscribe(*this, MethodPtr<&Registration::OnResponsePowParams>{}),
      protocol_context_.MessageEvent<ClientGlobalRegApi::ConfirmRegistration>()
          .Subscribe(*this, MethodPtr<&Registration::OnConfirmRegistration>{}),
      protocol_context_.MessageEvent<ClientApiRegSafe::ResolveServersResponse>()
          .Subscribe(*this,
                     MethodPtr<&Registration::OnResolveCloudResponse>{}));
}

Registration::~Registration() { AE_TELED_DEBUG("~Registration"); }

TimePoint Registration::Update(TimePoint current_time) {
  AE_TELED_DEBUG("Registration::Update UTC :{:%Y-%m-%d %H:%M:%S}",
                 current_time);

  // TODO: add check for actual packet sending or method timeouts

  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kSelectConnection:
        IterateConnection();
        break;
      case State::kWaitingConnection:
        break;
      case State::kConnected:
        Connected(current_time);
        break;
      case State::kGetKeys:
        GetKeys(current_time);
        break;
      case State::kGetPowParams:
        RequestPowParams(current_time);
        break;
      case State::kMakeRegistration:
        MakeRegistration(current_time);
        break;
      case State::kRequestCloudResolving:
        ResolveCloud(current_time);
        break;
      case State::kRegistered:
        Action::Result(*this);
        return current_time;
      case State::kRegistrationFailed:
        Action::Error(*this);
        return current_time;
      default:
        break;
    }
  }
  if (state_.get() == State::kWaitKeys) {
    return WaitKeys(current_time);
  }

  return current_time;
}

Client::ptr Registration::client() const { return client_; }

void Registration::IterateConnection() {
  if (server_channel_stream_ = connection_selection_->Update();
      !server_channel_stream_) {
    AE_TELED_ERROR("Server connection list is over");
    state_ = State::kRegistrationFailed;
    return;
  }

  if (server_channel_stream_->in().stream_info().is_linked) {
    state_ = State::kConnected;
    return;
  }

  connection_subscription_ =
      server_channel_stream_->in()
          .gate_update_event()
          .Subscribe([this]() {
            if (server_channel_stream_->in().stream_info().is_linked) {
              state_ = State::kConnected;
            } else {
              AE_TELED_ERROR("Connection error, try next");
              state_ = State::kSelectConnection;
            }
          })
          .Once();

  state_ = State::kWaitingConnection;
}

void Registration::Connected(TimePoint current_time) {
  AE_TELED_DEBUG("Registration::Connected at UTC :{:%Y-%m-%d %H:%M:%S}",
                 current_time);
  auto aether = aether_.Lock();
  if (!aether) {
    return;
  }

  // create simplest stream to server
  // On RequestPowParams will be created a more complicated one
  reg_server_stream_ = MakePtr<TiedStream>(
      ProtocolReadGate{protocol_context_, root_api_}, server_channel_stream_);

  state_ = State::kGetKeys;
}

void Registration::GetKeys(TimePoint current_time) {
  AE_TELED_DEBUG("Registration::GetKeys");
  state_ = State::kWaitKeys;
  auto packet = PacketBuilder{
      protocol_context_,
      PackMessage{
          RootApi{},
          RootApi::GetAsymmetricPublicKey{
              {}, RequestId::GenRequestId(), kDefaultCryptoLibProfile},
      },
  };

  packet_write_action_ =
      reg_server_stream_->in().Write(std::move(packet), current_time);

  // on error try repeat
  raw_transport_send_action_subscription_ =
      packet_write_action_->SubscribeOnError(
          [this](auto const&) { state_ = State::kSelectConnection; });
  last_request_time_ = current_time;
}

TimePoint Registration::WaitKeys(TimePoint current_time) {
  if (last_request_time_ + response_timeout_ < current_time) {
    AE_TELED_DEBUG("Registration::WaitKeys: timeout");
    state_ = State::kGetKeys;
    return current_time;
  }
  return last_request_time_ + response_timeout_;
}

void Registration::OnGetKeysResponse(
    MessageEventData<ClientApiRegSafe::GetKeysResponse> const& msg) {
  AE_TELED_DEBUG("Registration::OnGetKeysResponse");
  auto const& message = msg.message();

  auto r = CryptoSignVerify(message.signed_key.sign, message.signed_key.key,
                            sign_pk_);
  if (!r) {
    AE_TELED_ERROR("Registration::OnGetKeysResponse: Sign verification failed");
    state_ = State::kRegistrationFailed;
    return;
  }

  server_pub_key_ = std::move(message.signed_key.key);

  state_ = State::kGetPowParams;
}

void Registration::RequestPowParams(TimePoint current_time) {
  AE_TELED_DEBUG("Registration::RequestPowParams");

  Key secret_key;
  [[maybe_unused]] auto r = CryptoSyncKeygen(secret_key);
  assert(r);

  server_async_key_provider_ = MakePtr<RegistrationAsyncKeyProvider>();
  server_async_key_provider_->set_public_key(server_pub_key_);

  server_sync_key_provider_ = MakePtr<RegistrationSyncKeyProvider>();
  server_sync_key_provider_->set_key(secret_key);

  reg_server_stream_ = CreateRegServerStream(
      StreamIdGenerator::GetNextClientStreamId(), server_async_key_provider_,
      server_sync_key_provider_);

  packet_write_action_ = reg_server_stream_->in().Write(
      PacketBuilder{
          protocol_context_,
          PackMessage{
              ServerRegistrationApi{},
              ServerRegistrationApi::RequestProofOfWorkData{
                  {},
                  RequestId::GenRequestId(),
                  parent_uid_,
                  PowMethod::kBCryptCrc32,
                  std::move(secret_key),
              },
          },
      },
      current_time);

  reg_server_write_subscription_ =
      packet_write_action_
          ->SubscribeOnError([this](auto const&) {
            AE_TELED_ERROR("RequestPowParams stream write failed");
            state_ = State::kRegistrationFailed;
          })
          .Once();
}

void Registration::OnResponsePowParams(
    MessageEventData<ClientApiRegSafe::ResponseWorkProofData> const& msg) {
  AE_TELED_DEBUG("Registration::OnResponsePowParams");
  auto const& message = msg.message();
  [[maybe_unused]] auto res =
      CryptoSignVerify(message.pow_params.global_key.sign,
                       message.pow_params.global_key.key, sign_pk_);
  if (!res) {
    AE_TELED_ERROR(
        "Registration::OnResponsePowParams: Sign verification failed");
    state_ = State::kRegistrationFailed;
    return;
  }

  aether_global_key_ = message.pow_params.global_key.key;

  pow_params_.salt = message.pow_params.salt;
  pow_params_.max_hash_value = message.pow_params.max_hash_value;
  pow_params_.password_suffix = message.pow_params.password_suffix;
  pow_params_.pool_size = message.pow_params.pool_size;

  state_ = State::kMakeRegistration;
}

void Registration::MakeRegistration(TimePoint current_time) {
  AE_TELED_DEBUG("Registration::MakeRegistration");

  // Proof calculation
  // TODO: move into update method to perform in a limited duration.
  // NOT TODO: don't send back iterations count and time. An attacker can send
  // wrong numbers.
  auto proofs = ProofOfWork::ComputeProofOfWork(
      pow_params_.pool_size, pow_params_.salt, pow_params_.password_suffix,
      pow_params_.max_hash_value);

  [[maybe_unused]] auto r = CryptoSyncKeygen(master_key_);
  assert(r);

  AE_TELE_DEBUG("Client registered", "Global Pk: {}:{}, Masterkey: {}:{}",
                aether_global_key_.Index(), aether_global_key_,
                master_key_.Index(), master_key_);

  auto global_async_key_provider = MakePtr<RegistrationAsyncKeyProvider>();
  global_async_key_provider->set_public_key(aether_global_key_);

  auto global_sync_key_provider = MakePtr<RegistrationSyncKeyProvider>();
  global_sync_key_provider->set_key(master_key_);

  auto stream_id = StreamIdGenerator::GetNextClientStreamId();
  global_reg_server_stream_ = CreateGlobalRegServerStream(
      stream_id,
      ServerRegistrationApi::Registration{{},
                                          stream_id,
                                          pow_params_.salt,
                                          pow_params_.password_suffix,
                                          std::move(proofs),
                                          parent_uid_,
                                          server_sync_key_provider_->GetKey()},
      std::move(global_async_key_provider),
      std::move(global_sync_key_provider));

  Tie(*global_reg_server_stream_, *reg_server_stream_);

  packet_write_action_ = global_reg_server_stream_->in().Write(
      PacketBuilder{
          protocol_context_,
          PackMessage{
              GlobalRegServerApi{},
              GlobalRegServerApi::SetMasterKey{{}, master_key_},
              GlobalRegServerApi::Finish{{}, RequestId::GenRequestId()},
          },
      },
      current_time);

  reg_server_write_subscription_ =
      packet_write_action_
          ->SubscribeOnError([this](auto const&) {
            AE_TELED_ERROR("MakeRegistration stream write failed");
            state_ = State::kRegistrationFailed;
          })
          .Once();
}

void Registration::OnConfirmRegistration(
    MessageEventData<ClientGlobalRegApi::ConfirmRegistration> const& msg) {
  auto const& message = msg.message();
  AE_TELED_DEBUG("Registration::OnConfirmRegistration\n\tservers count {}",
                 message.registration_response.cloud.size());

  ephemeral_uid_ = message.registration_response.ephemeral_uid;
  uid_ = message.registration_response.uid;
  cloud_ = message.registration_response.cloud;

  state_ = State::kRequestCloudResolving;
}

void Registration::ResolveCloud(TimePoint current_time) {
  AE_TELED_DEBUG("Registration::ResolveCloud");

  packet_write_action_ = reg_server_stream_->in().Write(
      PacketBuilder{
          protocol_context_,
          PackMessage{
              ServerRegistrationApi{},
              ServerRegistrationApi::ResolveServers{
                  {},
                  RequestId::GenRequestId(),
                  cloud_,
              },
          },
      },
      current_time);

  reg_server_write_subscription_ =
      packet_write_action_
          ->SubscribeOnError([this](auto const&) {
            AE_TELED_ERROR("ResolveCloud stream write failed");
            state_ = State::kRegistrationFailed;
          })
          .Once();
}

void Registration::OnResolveCloudResponse(
    MessageEventData<ClientApiRegSafe::ResolveServersResponse> const& msg) {
  auto const& message = msg.message();
  AE_TELED_DEBUG("Registration::OnResolveCloudResponse\n\tservers count {}",
                 message.servers.size());

  auto aether = aether_.Lock();
  if (!aether) {
    return;
  }

  Cloud::ptr new_cloud = aether->domain_->LoadCopy(aether->cloud_prefab);
  assert(new_cloud);

  for (const auto& description : message.servers) {
    auto server_id = description.server_id;
    auto cached_server = aether->GetServer(server_id);
    if (cached_server) {
      AE_TELED_DEBUG("Use cached server");
      new_cloud->AddServer(cached_server);
      continue;
    }

    AE_TELED_DEBUG("Create new server id: {}", server_id);

    Server::ptr server = aether->domain_->CreateObj<Server>();
    assert(server);
    server->server_id = server_id;

    for (const auto& i : description.ips) {
      for (const auto& protocol_port : i.protocol_and_ports) {
        AE_TELED_DEBUG("Add channel ip {}, port {} protocol {}", i.ip,
                       protocol_port.port, protocol_port.protocol);
        auto channel = server->domain_->CreateObj<Channel>();
        assert(channel);

        channel->address = IpAddressPortProtocol{{i.ip, protocol_port.port},
                                                 protocol_port.protocol};
        server->AddChannel(std::move(channel));
      }
    }
    new_cloud->AddServer(server);
    aether->AddServer(std::move(server));
  }

  client_->SetConfig(uid_, ephemeral_uid_, master_key_, std::move(new_cloud));

  AE_TELED_DEBUG("Client registered with uid {} and ephemeral uid {}",
                 client_->uid(), client_->ephemeral_uid());

  state_ = State::kRegistered;
}

Ptr<ByteStream> Registration::CreateRegServerStream(
    StreamId stream_id, Ptr<IAsyncKeyProvider> async_key_provider,
    Ptr<ISyncKeyProvider> sync_key_provider) {
  auto aether = aether_.Lock();
  if (!aether) {
    return {};
  }

  auto tied_stream = MakePtr<TiedStream>(
      ProtocolReadGate{protocol_context_, ClientApiRegSafe{}},
      CryptoGate{MakePtr<AsyncEncryptProvider>(std::move(async_key_provider)),
                 MakePtr<SyncDecryptProvider>(std::move(sync_key_provider))},
      StreamApiGate{protocol_context_, stream_id},
      ProtocolWriteGate{protocol_context_, RootApi{},
                        RootApi::Enter{
                            {},
                            stream_id,
                            kDefaultCryptoLibProfile,
                        }},
      ProtocolReadGate{protocol_context_, root_api_}, server_channel_stream_);

  return tied_stream;
}

Ptr<ByteStream> Registration::CreateGlobalRegServerStream(
    StreamId stream_id, ServerRegistrationApi::Registration message,
    Ptr<IAsyncKeyProvider> global_async_key_provider,
    Ptr<ISyncKeyProvider> global_sync_key_provider) {
  auto tied_stream = MakePtr<TiedStream>(
      ProtocolReadGate{protocol_context_, ClientGlobalRegApi{}},
      CryptoGate{
          MakePtr<AsyncEncryptProvider>(std::move(global_async_key_provider)),
          MakePtr<SyncDecryptProvider>(std::move(global_sync_key_provider))},
      StreamApiGate{protocol_context_, stream_id},
      ProtocolWriteGate{protocol_context_, ServerRegistrationApi{},
                        std::move(message)});

  return tied_stream;
}

}  // namespace ae

#endif
