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

#  include "aether/aether.h"
#  include "aether/common.h"
#  include "aether/obj/domain.h"
#  include "aether/crypto/sign.h"
#  include "aether/crypto/key_gen.h"
#  include "aether/crypto/crypto_definitions.h"

#  include "aether/stream_api/tied_stream.h"
#  include "aether/stream_api/crypto_stream.h"
#  include "aether/stream_api/protocol_stream.h"
#  include "aether/stream_api/event_subscribe_gate.h"

#  include "aether/crypto/sync_crypto_provider.h"
#  include "aether/crypto/async_crypto_provider.h"

#  include "aether/api_protocol/api_context.h"

#  include "aether/proof_of_work.h"
#  include "aether/server_list/no_filter_server_list_policy.h"
#  include "aether/ae_actions/registration/registration_key_provider.h"

#  include "aether/ae_actions/ae_actions_tele.h"

namespace ae {

Registration::Registration(ActionContext action_context, PtrView<Aether> aether,
                           Uid parent_uid, Client::ptr client)
    : Action{action_context},
      aether_{std::move(aether)},
      client_{std::move(client)},
      parent_uid_{std::move(parent_uid)},
      client_root_api_{protocol_context_},
      client_safe_api_{protocol_context_},
      client_global_reg_api_{protocol_context_},
      server_reg_root_api_{protocol_context_, action_context},
      server_reg_api_{protocol_context_, action_context},
      global_reg_server_api_{protocol_context_, action_context},
      state_{State::kSelectConnection},
      // TODO: add configuration
      response_timeout_{std::chrono::seconds(20)},
      sign_pk_{aether_.Lock()->crypto->signs_pk_[kDefaultSignatureMethod]} {
  AE_TELE_INFO(RegisterStarted);

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
      make_unique<ServerList>(make_unique<NoFilterServerListPolicy>(), cloud);
  connection_selection_ =
      AsyncForLoop<std::unique_ptr<ServerChannelStream>>::Construct(
          *server_list_, [this, aether_ptr, adapter{adapter}]() {
            auto item = server_list_->Get();
            return make_unique<ServerChannelStream>(
                aether_ptr, adapter, item.server(), item.channel());
          });

  // trigger action on state change
  state_change_subscription_ =
      state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
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
    AE_TELE_ERROR(RegisterServerConnectionListOver,
                  "Server connection list is over");
    state_ = State::kRegistrationFailed;
    return;
  }

  if (server_channel_stream_->in().stream_info().is_linked) {
    state_ = State::kConnected;
    return;
  }

  connection_subscription_ =
      server_channel_stream_->in().gate_update_event().Subscribe([this]() {
        if (server_channel_stream_->in().stream_info().is_linked) {
          state_ = State::kConnected;
        } else {
          AE_TELE_WARNING(RegisterConnectionErrorTryNext,
                          "Connection error, try next");
          state_ = State::kSelectConnection;
        }
      });

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
  reg_server_stream_ = make_unique<TiedStream>(
      ProtocolReadGate{protocol_context_, client_root_api_},
      *server_channel_stream_);

  state_ = State::kGetKeys;
}

void Registration::GetKeys(TimePoint current_time) {
  AE_TELE_INFO(RegisterGetKeys, "GetKeys {:%Y-%m-%d %H:%M:%S}", current_time);

  auto packet_context = ApiContext{protocol_context_, server_reg_root_api_};
  auto get_key_promise =
      packet_context->get_asymmetric_public_key(kDefaultCryptoLibProfile);

  subscriptions_.Push(
      get_key_promise->ResultEvent().Subscribe([&](auto& promise) {
        auto& signed_key = promise.value();
        auto r = CryptoSignVerify(signed_key.sign, signed_key.key, sign_pk_);
        if (!r) {
          AE_TELE_ERROR(RegisterGetKeysVerificationFailed,
                        "Sign verification failed");
          state_ = State::kRegistrationFailed;
          return;
        }

        server_pub_key_ = std::move(signed_key.key);
        state_ = State::kGetPowParams;
      }),
      get_key_promise->ErrorEvent().Subscribe(
          [&](auto const&) { state_ = State::kRegistrationFailed; }));

  packet_write_action_ =
      reg_server_stream_->in().Write(std::move(packet_context), current_time);

  // on error try repeat
  raw_transport_send_action_subscription_ =
      packet_write_action_->ErrorEvent().Subscribe(
          [this](auto const&) { state_ = State::kSelectConnection; });
  last_request_time_ = current_time;

  state_ = State::kWaitKeys;
}

TimePoint Registration::WaitKeys(TimePoint current_time) {
  if (last_request_time_ + response_timeout_ < current_time) {
    AE_TELE_WARNING(RegisterWaitKeysTimeout,
                    "WaitKeys timeout {:%Y-%m-%d %H:%M:%S}", current_time);
    state_ = State::kSelectConnection;
    return current_time;
  }
  return last_request_time_ + response_timeout_;
}

void Registration::RequestPowParams(TimePoint current_time) {
  AE_TELE_INFO(RegisterRequestPowParams, "Request proof of work params");

  Key secret_key;
  [[maybe_unused]] auto r = CryptoSyncKeygen(secret_key);
  assert(r);

  auto server_async_key_provider = make_unique<RegistrationAsyncKeyProvider>();
  server_async_key_provider->set_public_key(server_pub_key_);
  server_async_key_provider_ = server_async_key_provider.get();

  auto server_sync_key_provider = make_unique<RegistrationSyncKeyProvider>();
  server_sync_key_provider->set_key(secret_key);
  server_sync_key_provider_ = server_sync_key_provider.get();

  reg_server_stream_ =
      CreateRegServerStream(std::move(server_async_key_provider),
                            std::move(server_sync_key_provider));

  auto api_context = ApiContext{protocol_context_, server_reg_api_};
  auto pow_params_promise = api_context->request_proof_of_work_data(
      parent_uid_, PowMethod::kBCryptCrc32, std::move(secret_key));

  subscriptions_.Push(
      pow_params_promise->ResultEvent().Subscribe([&](auto& promise) {
        auto& pow_params = promise.value();
        [[maybe_unused]] auto res = CryptoSignVerify(
            pow_params.global_key.sign, pow_params.global_key.key, sign_pk_);
        if (!res) {
          AE_TELE_ERROR(RegisterPowParamsVerificationFailed,
                        "Proof of work params sign verification failed");
          state_ = State::kRegistrationFailed;
          return;
        }

        aether_global_key_ = pow_params.global_key.key;

        pow_params_.salt = pow_params.salt;
        pow_params_.max_hash_value = pow_params.max_hash_value;
        pow_params_.password_suffix = pow_params.password_suffix;
        pow_params_.pool_size = pow_params.pool_size;

        state_ = State::kMakeRegistration;
      }),
      pow_params_promise->ErrorEvent().Subscribe(
          [&](auto const&) { state_ = State::kRegistrationFailed; }));

  packet_write_action_ =
      reg_server_stream_->in().Write(std::move(api_context), current_time);

  reg_server_write_subscription_ =
      packet_write_action_->ErrorEvent().Subscribe([this](auto const&) {
        AE_TELED_ERROR("RequestPowParams stream write failed");
        state_ = State::kRegistrationFailed;
      });
}

void Registration::MakeRegistration(TimePoint current_time) {
  AE_TELE_INFO(RegisterMakeRegistration, "Make registration");

  // Proof calculation
  // TODO: move into update method to perform in a limited duration.
  // NOT TODO: don't send back iterations count and time. An attacker can send
  // wrong numbers.
  auto proofs = ProofOfWork::ComputeProofOfWork(
      pow_params_.pool_size, pow_params_.salt, pow_params_.password_suffix,
      pow_params_.max_hash_value);

  [[maybe_unused]] auto r = CryptoSyncKeygen(master_key_);
  assert(r);

  AE_TELE_DEBUG(RegisterKeysGenerated, "Global Pk: {}:{}, Masterkey: {}:{}",
                aether_global_key_.Index(), aether_global_key_,
                master_key_.Index(), master_key_);

  auto global_async_key_provider = make_unique<RegistrationAsyncKeyProvider>();
  global_async_key_provider->set_public_key(aether_global_key_);

  auto global_sync_key_provider = make_unique<RegistrationSyncKeyProvider>();
  global_sync_key_provider->set_key(master_key_);

  global_reg_server_stream_ = CreateGlobalRegServerStream(
      std::move(global_async_key_provider), std::move(global_sync_key_provider),
      ProtocolWriteMessageGate<DataBuffer>{
          protocol_context_,
          [&, pow_params{pow_params_}, proofs{std::move(proofs)}](
              ProtocolContext& context, DataBuffer&& data) -> DataBuffer {
            auto api_context = ApiContext{context, server_reg_api_};
            api_context->registration(
                pow_params.salt, pow_params.password_suffix, proofs,
                parent_uid_, server_sync_key_provider_->GetKey(),
                std::move(data));
            return api_context;
          }});

  Tie(*global_reg_server_stream_, *reg_server_stream_);

  auto api_context = ApiContext{protocol_context_, global_reg_server_api_};
  api_context->set_master_key(master_key_);
  auto confirm_promise = api_context->finish();

  subscriptions_.Push(
      confirm_promise->ResultEvent().Subscribe([&](auto& promise) {
        auto& reg_response = promise.value();
        AE_TELE_INFO(RegisterRegistrationConfirmed, "Registration confirmed");

        ephemeral_uid_ = reg_response.ephemeral_uid;
        uid_ = reg_response.uid;
        cloud_ = reg_response.cloud;
        assert(cloud_.size() > 0);

        state_ = State::kRequestCloudResolving;
      }),
      confirm_promise->ErrorEvent().Subscribe(
          [&](auto const&) { state_ = State::kRegistrationFailed; }));

  packet_write_action_ = global_reg_server_stream_->in().Write(
      std::move(api_context), current_time);

  reg_server_write_subscription_ =
      packet_write_action_->ErrorEvent().Subscribe([this](auto const&) {
        AE_TELED_ERROR("MakeRegistration stream write failed");
        state_ = State::kRegistrationFailed;
      });
}

void Registration::ResolveCloud(TimePoint current_time) {
  AE_TELE_INFO(RegisterResolveCloud, "Resolve cloud with {} servers",
               cloud_.size());

  auto api_context = ApiContext{protocol_context_, server_reg_api_};
  auto resolve_cloud_promise = api_context->resolve_servers(cloud_);

  subscriptions_.Push(
      resolve_cloud_promise->ResultEvent().Subscribe(
          [&](auto& promise) { OnCloudResolved(promise.value()); }),
      resolve_cloud_promise->ErrorEvent().Subscribe(
          [&](auto const&) { state_ = State::kRegistrationFailed; }));

  packet_write_action_ =
      reg_server_stream_->in().Write(std::move(api_context), current_time);

  reg_server_write_subscription_ =
      packet_write_action_->ErrorEvent().Subscribe([this](auto const&) {
        AE_TELED_ERROR("ResolveCloud stream write failed");
        state_ = State::kRegistrationFailed;
      });
}

void Registration::OnCloudResolved(
    std::vector<ServerDescriptor> const& servers) {
  AE_TELE_INFO(RegisterResolveCloudResponse);

  auto aether = aether_.Lock();
  if (!aether) {
    return;
  }

  Cloud::ptr new_cloud = aether->domain_->LoadCopy(aether->cloud_prefab);
  assert(new_cloud);

  for (const auto& description : servers) {
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

  AE_TELE_DEBUG(RegisterClientRegistered,
                "Client registered with uid {} and ephemeral uid {}",
                client_->uid(), client_->ephemeral_uid());
  state_ = State::kRegistered;
}

std::unique_ptr<ByteStream> Registration::CreateRegServerStream(
    std::unique_ptr<IAsyncKeyProvider> async_key_provider,
    std::unique_ptr<ISyncKeyProvider> sync_key_provider) {
  auto aether = aether_.Lock();
  if (!aether) {
    return {};
  }

  auto tied_stream = make_unique<TiedStream>(
      ProtocolReadGate{protocol_context_, client_safe_api_},
      DebugGate{"RegServer write {}", "RegServer read {}"},
      CryptoGate{
          make_unique<AsyncEncryptProvider>(std::move(async_key_provider)),
          make_unique<SyncDecryptProvider>(std::move(sync_key_provider))},
      ProtocolWriteMessageGate<DataBuffer>{
          protocol_context_,
          [&](ProtocolContext& context, DataBuffer&& data) -> DataBuffer {
            auto api_context = ApiContext{context, server_reg_root_api_};
            api_context->enter(kDefaultCryptoLibProfile, std::move(data));
            return DataBuffer{std::move(api_context)};
          }},
      EventSubscribeGate<DataBuffer>{
          EventSubscriber{client_root_api_.enter_event}},
      ProtocolReadGate{protocol_context_, client_root_api_},
      server_channel_stream_);

  return tied_stream;
}

std::unique_ptr<ByteStream> Registration::CreateGlobalRegServerStream(
    std::unique_ptr<IAsyncKeyProvider> global_async_key_provider,
    std::unique_ptr<ISyncKeyProvider> global_sync_key_provider,
    ProtocolWriteMessageGate<DataBuffer> enter_global_api_gate) {
  auto tied_stream = make_unique<TiedStream>(
      ProtocolReadGate{protocol_context_, client_global_reg_api_},
      DebugGate{"GlobalRegServer write {}", "GlobalRegServer read {}"},
      CryptoGate{make_unique<AsyncEncryptProvider>(
                     std::move(global_async_key_provider)),
                 make_unique<SyncDecryptProvider>(
                     std::move(global_sync_key_provider))},
      EventSubscribeGate<DataBuffer>{
          EventSubscriber{client_safe_api_.global_api_event}},
      std::move(enter_global_api_gate));

  return tied_stream;
}

}  // namespace ae

#endif
