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
#  include "aether/crypto/sign.h"
#  include "aether/api_protocol/api_context.h"
#  include "aether/crypto/crypto_definitions.h"
#  include "aether/stream_api/api_call_adapter.h"

#  include "aether/registration/proof_of_work.h"
#  include "aether/registration/registration_crypto_provider.h"

#  include "aether/ae_actions/ae_actions_tele.h"

namespace ae {

Registration::Registration(ActionContext action_context, Aether& aether,
                           Ptr<RegistrationCloud> const& reg_cloud,
                           Uid parent_uid)
    : Action{action_context},
      action_context_{action_context},
      parent_uid_{std::move(parent_uid)},
      root_crypto_provider_{std::make_unique<RegistrationCryptoProvider>()},
      global_crypto_provider_{std::make_unique<RegistrationCryptoProvider>()},
      client_root_api_{protocol_context_, *root_crypto_provider_->decryptor(),
                       *global_crypto_provider_->decryptor()},
      server_reg_root_api_{protocol_context_, action_context_,
                           *root_crypto_provider_->encryptor(),
                           *global_crypto_provider_->encryptor()},
      root_server_select_stream_{action_context_, reg_cloud},
      state_{State::kInitConnection},
      // TODO: add configuration
      response_timeout_{std::chrono::seconds(20)},
      sign_pk_{aether.crypto->signs_pk_[kDefaultSignatureMethod]} {
  AE_TELE_INFO(RegisterStarted);

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
      case State::kInitConnection:
        InitConnection();
        break;
      case State::kRestreamConnection:
        Restream();
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

ClientConfig const& Registration::client_config() const {
  return client_config_;
}

void Registration::InitConnection() {
  AE_TELED_DEBUG("Registration::InitConnection");

  root_server_select_stream_.server_changed_event().Subscribe([this]() {
    // Reinit connection and start registration from the beginning
    AE_TELED_ERROR("Server changed");
    state_ = State::kInitConnection;
  });

  root_server_select_stream_.cloud_error_event().Subscribe([this]() {
    AE_TELED_ERROR("Link Error, Registration failed");
    state_ = State::kRegistrationFailed;
  });

  // Connect parser for out data
  data_read_subscription_ =
      root_server_select_stream_.out_data_event().Subscribe(
          [this](const auto& data) {
            auto parser = ApiParser{protocol_context_, data};
            parser.Parse(client_root_api_);
          });

  state_ = State::kGetKeys;
}

void Registration::Restream() {
  state_ = State::kInitConnection;
  root_server_select_stream_.Restream();
}

void Registration::GetKeys() {
  AE_TELE_INFO(RegisterGetKeys, "GetKeys");

  auto api_call = ApiCallAdapter{ApiContext{server_reg_root_api_},
                                 root_server_select_stream_};

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
      packet_write_action_->StatusEvent().Subscribe(
          OnError{[&]() { state_ = State::kRestreamConnection; }});
  last_request_time_ = Now();

  state_ = State::kWaitKeys;
}

TimePoint Registration::WaitKeys() {
  auto current_time = Now();
  if (last_request_time_ + response_timeout_ < current_time) {
    AE_TELE_WARNING(RegisterWaitKeysTimeout,
                    "WaitKeys timeout {:%Y-%m-%d %H:%M:%S}", current_time);
    state_ = State::kRestreamConnection;
    return current_time;
  }
  return last_request_time_ + response_timeout_;
}

void Registration::RequestPowParams() {
  AE_TELE_INFO(RegisterRequestPowParams, "Request proof of work params");

  // setup crypto config
  root_crypto_provider_->SetEncryptionKey(server_pub_key_);
  auto ret_key = root_crypto_provider_->GetDecryptionKey();

  auto api_call = ApiCallAdapter{ApiContext{server_reg_root_api_},
                                 root_server_select_stream_};
  api_call->enter(
      kDefaultCryptoLibProfile,
      SubApi{[this, &ret_key](ApiContext<ServerRegistrationApi>& server_api) {
        server_api->set_return_key(std::move(ret_key));
        auto pow_params_promise = server_api->request_proof_of_work_data(
            parent_uid_, PowMethod::kBCryptCrc32);
        response_sub_ =
            pow_params_promise->StatusEvent().Subscribe(ActionHandler{
                OnResult{[&](auto& promise) {
                  auto& pow_params = promise.value();
                  [[maybe_unused]] auto res =
                      CryptoSignVerify(pow_params.global_key.sign,
                                       pow_params.global_key.key, sign_pk_);
                  if (!res) {
                    AE_TELE_ERROR(
                        RegisterPowParamsVerificationFailed,
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
      }});

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

  global_crypto_provider_->SetEncryptionKey(aether_global_key_);
  client_config_.master_key = global_crypto_provider_->GetDecryptionKey();

  AE_TELE_DEBUG(RegisterKeysGenerated, "Global Pk: {}:{}, Masterkey: {}:{}",
                aether_global_key_.Index(), aether_global_key_,
                client_config_.master_key.Index(), client_config_.master_key);

  auto api_call = ApiCallAdapter{ApiContext{server_reg_root_api_},
                                 root_server_select_stream_};
  api_call->enter(
      kDefaultCryptoLibProfile,
      SubApi{[this, &proofs](ApiContext<ServerRegistrationApi>& server_api) {
        server_api->registration(
            pow_params_.salt, pow_params_.password_suffix, proofs, parent_uid_,
            SubApi{[this](ApiContext<GlobalRegServerApi>& global_api) {
              // set master key and wait for confirmation
              global_api->set_master_key(client_config_.master_key);
              auto confirm_promise = global_api->finish();

              response_sub_ =
                  confirm_promise->StatusEvent().Subscribe(ActionHandler{
                      OnResult{[&](auto& promise) {
                        auto& reg_response = promise.value();
                        AE_TELE_INFO(RegisterRegistrationConfirmed,
                                     "Registration confirmed");

                        client_config_.uid = reg_response.uid;
                        client_config_.ephemeral_uid =
                            reg_response.ephemeral_uid;
                        client_config_.parent_uid = parent_uid_;

                        // use client cloud_ for cloud resolve request
                        client_cloud_ = reg_response.cloud;
                        assert((client_cloud_.size() > 0) &&
                               "Client's cloud is empty");
                        state_ = State::kRequestCloudResolving;
                      }},
                      OnError{[&]() { state_ = State::kRegistrationFailed; }}});
            }});
      }});

  packet_write_action_ = api_call.Flush();

  reg_server_write_subscription_ =
      packet_write_action_->StatusEvent().Subscribe(OnError{[this]() {
        AE_TELED_ERROR("MakeRegistration stream write failed");
        state_ = State::kRegistrationFailed;
      }});
}

void Registration::ResolveCloud() {
  AE_TELE_INFO(RegisterResolveCloud, "Resolve cloud as [{}]", client_cloud_);

  auto api_call = ApiCallAdapter{ApiContext{server_reg_root_api_},
                                 root_server_select_stream_};
  api_call->enter(
      kDefaultCryptoLibProfile,
      SubApi{[this](ApiContext<ServerRegistrationApi>& server_api) {
        auto resolve_cloud_promise = server_api->resolve_servers(client_cloud_);
        response_sub_ =
            resolve_cloud_promise->StatusEvent().Subscribe(ActionHandler{
                OnResult{
                    [&](auto& promise) { OnCloudResolved(promise.value()); }},
                OnError{[&]() { state_ = State::kRegistrationFailed; }}});
      }});

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

  std::vector<Server::ptr> server_objects;
  server_objects.reserve(servers.size());

  for (const auto& description : servers) {
    std::vector<Endpoint> endpoints;
    for (auto const& ip : description.ips) {
      for (auto const& pp : ip.protocol_and_ports) {
        endpoints.emplace_back(Endpoint{{ip.ip, pp.port}, pp.protocol});
      }
    }
    AE_TELED_DEBUG("Got server {} with endpoints {}", description.server_id,
                   endpoints);
    client_config_.cloud.emplace_back(
        ServerConfig{description.server_id, std::move(endpoints)});
  }

  // sort cloud by order of client_cloud_
  std::sort(std::begin(client_config_.cloud), std::end(client_config_.cloud),
            [this](auto const& left, auto const& right) {
              auto left_order = std::find(std::begin(client_cloud_),
                                          std::end(client_cloud_), left.id);
              auto right_order = std::find(std::begin(client_cloud_),
                                           std::end(client_cloud_), right.id);
              return left_order < right_order;
            });

  AE_TELE_DEBUG(RegisterClientRegistered,
                "Client registered:\n>>>>>>>>>>>>>>>\n{}\n<<<<<<<<<<<<<<<",
                client_config_);
  state_ = State::kRegistered;
}
}  // namespace ae
#endif
