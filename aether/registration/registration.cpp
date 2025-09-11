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

#include "aether/registration/registration.h"

#if AE_SUPPORT_REGISTRATION

#  include <utility>

#  include "aether/aether.h"
#  include "aether/common.h"
#  include "aether/obj/domain.h"
#  include "aether/crypto/sign.h"
#  include "aether/crypto/key_gen.h"
#  include "aether/crypto/crypto_definitions.h"

#  include "aether/stream_api/byte_gate.h"
#  include "aether/stream_api/crypto_gate.h"
#  include "aether/stream_api/event_subscribe_gate.h"

#  include "aether/crypto/sync_crypto_provider.h"
#  include "aether/crypto/async_crypto_provider.h"

#  include "aether/api_protocol/api_context.h"

#  include "aether/registration/proof_of_work.h"
#  include "aether/registration/reg_server_stream.h"
#  include "aether/registration/registration_key_provider.h"
#  include "aether/registration/global_reg_server_stream.h"

#  include "aether/ae_actions/ae_actions_tele.h"

namespace ae {

Registration::Registration(ActionContext action_context,
                           ObjPtr<Aether> const& aether,
                           RegistrationCloud::ptr const& reg_cloud,
                           Uid parent_uid, Client::ptr client)
    : Action{action_context},
      action_context_{action_context},
      aether_{aether},
      client_{std::move(client)},
      parent_uid_{std::move(parent_uid)},
      client_root_api_{protocol_context_},
      client_safe_api_{protocol_context_},
      client_global_reg_api_{protocol_context_},
      server_reg_root_api_{protocol_context_, action_context_},
      server_reg_api_{protocol_context_, action_context_},
      global_reg_server_api_{protocol_context_, action_context_},
      reg_server_connection_pool_{action_context_, reg_cloud},
      state_{State::kSelectConnection},
      // TODO: add configuration
      response_timeout_{std::chrono::seconds(20)},
      sign_pk_{aether_.Lock()->crypto->signs_pk_[kDefaultSignatureMethod]} {
  AE_TELE_INFO(RegisterStarted);

  auto aether_ptr = aether_.Lock();
  assert(aether_ptr);

  // parent uid must not be empty
  assert(!parent_uid_.empty());

  // trigger action on state change
  state_change_subscription_ =
      state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
}

Registration::~Registration() { AE_TELED_DEBUG("~Registration"); }

UpdateStatus Registration::Update() {
  AE_TELED_DEBUG("Registration::Update state {}", state_.get());

  // TODO: add check for actual packet sending or method timeouts

  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kSelectConnection:
        IterateConnection();
        break;
      case State::kWaitingConnection:
        break;
      case State::kConnectionFailed:
        ConnectionFailed();
        break;
      case State::kConnected:
        Connected();
        break;
      case State::kGetKeys:
        GetKeys();
        break;
      case State::kGetPowParams:
        RequestPowParams();
        break;
      case State::kMakeRegistration:
        MakeRegistration();
        break;
      case State::kRequestCloudResolving:
        ResolveCloud();
        break;
      case State::kRegistered:
        return UpdateStatus::Result();
      case State::kRegistrationFailed:
        return UpdateStatus::Error();
      default:
        break;
    }
  }
  if (state_.get() == State::kWaitKeys) {
    return UpdateStatus::Delay(WaitKeys());
  }

  return {};
}

Client::ptr Registration::client() const { return client_; }

void Registration::IterateConnection() {
  if (server_channel_ = reg_server_connection_pool_.TopConnection();
      server_channel_ == nullptr) {
    AE_TELE_ERROR(RegisterServerConnectionListOver,
                  "Server connection list is over");
    state_ = State::kRegistrationFailed;
    return;
  }

  AE_TELED_DEBUG("Server selected");
  if (server_channel_->stream().stream_info().link_state ==
      LinkState::kLinked) {
    state_ = State::kConnected;
    return;
  }

  connection_subscription_ =
      server_channel_->stream().stream_update_event().Subscribe([this]() {
        if (server_channel_->stream().stream_info().link_state ==
            LinkState::kLinked) {
          state_ = State::kConnected;
        } else {
          AE_TELE_WARNING(RegisterConnectionErrorTryNext,
                          "Connection error, try next");
          state_ = State::kConnectionFailed;
        }
      });

  state_ = State::kWaitingConnection;
}

void Registration::Connected() {
  AE_TELED_DEBUG("Registration::Connected");

  // create simplest stream to server
  // On RequestPowParams will be created a more complicated one
  reg_server_stream_ = make_unique<GatesStream>(
      ByteGate{}, ProtocolReadGate{protocol_context_, client_root_api_});

  Tie(*reg_server_stream_, server_channel_->stream());

  state_ = State::kGetKeys;
}

void Registration::ConnectionFailed() {
  AE_TELED_DEBUG("Registration::ConnectionFailed");
  if (!reg_server_connection_pool_.Rotate()) {
    state_ = State::kRegistrationFailed;
  } else {
    state_ = State::kSelectConnection;
  }
}

void Registration::GetKeys() {
  AE_TELE_INFO(RegisterGetKeys, "GetKeys");

  auto api_call = ApiCallAdapter{
      ApiContext{protocol_context_, server_reg_root_api_}, *reg_server_stream_};
  auto get_key_promise =
      api_call->get_asymmetric_public_key(kDefaultCryptoLibProfile);

  response_sub_ = get_key_promise->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&](auto& promise) {
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
      }},
      OnError{[&]() { state_ = State::kRegistrationFailed; }},
  });

  packet_write_action_ = api_call.Flush();

  // on error try repeat
  raw_transport_send_action_subscription_ =
      packet_write_action_->StatusEvent().Subscribe(OnError{[&]() {
        reg_server_connection_pool_.Rotate();
        state_ = State::kConnectionFailed;
      }});
  last_request_time_ = Now();

  state_ = State::kWaitKeys;
}

TimePoint Registration::WaitKeys() {
  auto current_time = Now();
  if (last_request_time_ + response_timeout_ < current_time) {
    AE_TELE_WARNING(RegisterWaitKeysTimeout,
                    "WaitKeys timeout {:%Y-%m-%d %H:%M:%S}", current_time);
    state_ = State::kConnectionFailed;
    return current_time;
  }
  return last_request_time_ + response_timeout_;
}

void Registration::RequestPowParams() {
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

  reg_server_stream_ = std::make_unique<RegServerStream>(
      action_context_, protocol_context_, client_safe_api_,
      kDefaultCryptoLibProfile,
      make_unique<AsyncEncryptProvider>(std::move(server_async_key_provider)),
      make_unique<SyncDecryptProvider>(std::move(server_sync_key_provider)));

  Tie(*reg_server_stream_, server_channel_->stream());

  auto api_call = ApiCallAdapter{ApiContext{protocol_context_, server_reg_api_},
                                 *reg_server_stream_};
  auto pow_params_promise = api_call->request_proof_of_work_data(
      parent_uid_, PowMethod::kBCryptCrc32, std::move(secret_key));

  response_sub_ = pow_params_promise->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&](auto& promise) {
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
      }},
      OnError{[&]() { state_ = State::kRegistrationFailed; }}});

  packet_write_action_ = api_call.Flush();

  reg_server_write_subscription_ =
      packet_write_action_->StatusEvent().Subscribe(OnError{[this]() {
        AE_TELED_ERROR("RequestPowParams stream write failed");
        state_ = State::kRegistrationFailed;
      }});
}

void Registration::MakeRegistration() {
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

  global_reg_server_stream_ = std::make_unique<GlobalRegServerStream>(
      protocol_context_, client_global_reg_api_, server_reg_api_,
      pow_params_.salt, pow_params_.password_suffix, std::move(proofs),
      parent_uid_, server_sync_key_provider_->GetKey(),
      make_unique<AsyncEncryptProvider>(std::move(global_async_key_provider)),
      make_unique<SyncDecryptProvider>(std::move(global_sync_key_provider)));

  Tie(*global_reg_server_stream_, *reg_server_stream_);

  auto api_call =
      ApiCallAdapter{ApiContext{protocol_context_, global_reg_server_api_},
                     *global_reg_server_stream_};
  api_call->set_master_key(master_key_);
  auto confirm_promise = api_call->finish();

  response_sub_ = confirm_promise->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&](auto& promise) {
        auto& reg_response = promise.value();
        AE_TELE_INFO(RegisterRegistrationConfirmed, "Registration confirmed");

        ephemeral_uid_ = reg_response.ephemeral_uid;
        uid_ = reg_response.uid;
        client_cloud_ = reg_response.cloud;
        assert(client_cloud_.size() > 0);

        state_ = State::kRequestCloudResolving;
      }},
      OnError{[&]() { state_ = State::kRegistrationFailed; }}});

  packet_write_action_ = api_call.Flush();

  reg_server_write_subscription_ =
      packet_write_action_->StatusEvent().Subscribe(OnError{[this]() {
        AE_TELED_ERROR("MakeRegistration stream write failed");
        state_ = State::kRegistrationFailed;
      }});
}

void Registration::ResolveCloud() {
  AE_TELE_INFO(RegisterResolveCloud, "Resolve cloud with {} servers",
               client_cloud_.size());

  auto api_call = ApiCallAdapter{ApiContext{protocol_context_, server_reg_api_},
                                 *reg_server_stream_};
  auto resolve_cloud_promise = api_call->resolve_servers(client_cloud_);

  response_sub_ = resolve_cloud_promise->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&](auto& promise) { OnCloudResolved(promise.value()); }},
      OnError{[&]() { state_ = State::kRegistrationFailed; }}});

  packet_write_action_ = api_call.Flush();

  reg_server_write_subscription_ =
      packet_write_action_->StatusEvent().Subscribe(OnError{[this]() {
        AE_TELED_ERROR("ResolveCloud stream write failed");
        state_ = State::kRegistrationFailed;
      }});
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
  // TODO: this temporary
  auto adapter = aether->adapter_factories[0];

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
        auto channel = server->domain_->CreateObj<Channel>(adapter);
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
}  // namespace ae

#endif
