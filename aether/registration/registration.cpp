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

#  include <cstdint>
#  include <utility>

#  include "aether-miscpp/misc/override.h"
#  include "aether/aether.h"
#  include "aether/api_protocol/api_protocol.h"


#  include "aether/crypto.h"
#  include "aether/crypto/crypto_definitions.h"
#  include "aether/crypto/sign.h"
#  include "aether/executors/executors.h"

#  include "aether/registration/proof_of_work.h"
#  include "aether/registration/registration_crypto_provider.h"

#  include "aether/ae_actions/ae_actions_tele.h"

namespace ae {

Registration::Registration(AeContext const& ae_context,
                           Ptr<RegistrationCloud> const& reg_cloud,
                           Uid parent_uid)
    : Action{ae_context},
      ae_context_{ae_context},
      parent_uid_{std::move(parent_uid)},
      protocol_context_{ae_context_},
      root_crypto_provider_{},
      global_crypto_provider_{},

      client_root_api_{ae_context_.event_system(),
                       *root_crypto_provider_.decryptor(),
                       *global_crypto_provider_.decryptor()},

      server_reg_root_api_{*root_crypto_provider_.encryptor(),
                           *global_crypto_provider_.encryptor()},
      root_server_select_stream_{ae_context_, reg_cloud},
      // TODO: add configuration
      response_timeout_{std::chrono::seconds(20)},
      sign_pk_{Crypto::ptr{ae_context_.aether().crypto}
                   ->signs_pk_[kDefaultSignatureMethod]},
      registration_event_{ae_context_} {
  AE_TELE_INFO(RegisterStarted);

  // parent uid must not be empty
  assert(!parent_uid_.empty());


  InitConnection();

  server_pub_key_sub_ = client_root_api_.server_pub_key_event.Subscribe(
      [this](SignedKey const& signed_key) {
        if (!CryptoSignVerify(signed_key.sign, signed_key.key, sign_pk_)) {
          AE_TELE_ERROR(RegisterGetKeysVerificationFailed,
                        "Sign verification failed");
          return;
        }
        server_pub_key_ = signed_key.key;
        root_crypto_provider_.SetEncryptionKey(server_pub_key_);
        AE_TELED_INFO("Key received");
        RequestPowParams();
      });

  pow_params_sub_ = client_root_api_.pow_params_event.Subscribe(
      [this](PowParams const& params) {
        if (!CryptoSignVerify(params.global_key.sign, params.global_key.key,
                              sign_pk_)) {
          AE_TELE_ERROR(RegisterGetKeysVerificationFailed,
                        "Global key sign verification failed");
          return;
        }
        pow_params_ = params;
        aether_global_key_ = params.global_key.key;
        global_crypto_provider_.SetEncryptionKey(aether_global_key_);
        master_key_ = global_crypto_provider_.GetDecryptionKey();
        MakeRegistration();
      });

  reg_direct_sub_ = client_root_api_.registration_direct_event.Subscribe(
      [this](RegistrationDirectResult const& result) {
        ephemeral_uid_ = result.ephemeral_uid;
        client_uid_ = result.uid;
        client_cloud_.clear();
        for (auto const& sid : result.cloud) client_cloud_.push_back(sid);
        ResolveCloud();
      });
  resolved_servers_sub_ = client_root_api_.resolved_servers_event.Subscribe(
      [this](std::vector<ServerDescriptor> const& servers) {
        auto cfg = OnCloudResolved(servers);
        registration_event_.Emit(Ok(std::move(cfg)));
        Finish();
      });


  GetKeys();
}

Registration::~Registration() { AE_TELED_DEBUG("~Registration"); }

Registration::RegistrationEvent const& Registration::registration() {
  return registration_event_;
}

void Registration::InitConnection() {
  AE_TELED_DEBUG("Registration::InitConnection");

  root_server_select_stream_.server_changed_event().Subscribe([]() {
    // Reinit connection and start registration from the beginning
    // TODO: make async_waiter_ stop and start get keys again
    AE_TELED_ERROR("Server changed");
  });

  root_server_select_stream_.cloud_error_event().Subscribe([]() {
    AE_TELED_ERROR("Link Error, Registration failed");
    // TODO: make async_waiter_ stop with error
  });

  // Connect parser for out data
  root_server_select_stream_.out_data_event().Subscribe(
      [this](auto const& data) {
        auto parser = ApiParser{protocol_context_, data};
        parser.Parse(client_root_api_);
      });
}

void Registration::Restream() { root_server_select_stream_.Restream(); }



void Registration::GetKeys() {
  AE_TELE_INFO(RegisterGetKeys, "GetKeys");
  auto api_context =
      ApiContext<RegistrationRootApi>{server_reg_root_api_, protocol_context_};
  api_context->GetPublicKey(kDefaultCryptoLibProfile);
  root_server_select_stream_.Write(DataBuffer{std::move(api_context)});
}





void Registration::RequestPowParams() {
  AE_TELE_INFO(RegisterRequestPowParams, "Request proof of work params");
  auto api_context =
      ApiContext<RegistrationRootApi>{server_reg_root_api_, protocol_context_};
  api_context->Enter(
      kDefaultCryptoLibProfile,
      SubApi{[this](ApiContext<ServerRegistrationApi>& server_api) {
        server_api->SetReturnKey(root_crypto_provider_.GetDecryptionKey());
        server_api->RequestProofOfWorkData(parent_uid_, PowMethod::kBCryptCrc32);
      }});
  root_server_select_stream_.Write(DataBuffer{std::move(api_context)});
}





void Registration::MakeRegistration() {
  AE_TELE_INFO(RegisterMakeRegistration, "Make registration");

  auto proofs = ProofOfWork::ComputeProofOfWork(
      pow_params_.pool_size, pow_params_.salt, pow_params_.password_suffix,
      pow_params_.max_hash_value);

  AE_TELE_DEBUG(RegisterKeysGenerated, "Global Pk: {}:{}, Masterkey: {}:{}",
                aether_global_key_.Index(), aether_global_key_, master_key_.Index(),
                master_key_);

  auto api_context =
      ApiContext<RegistrationRootApi>{server_reg_root_api_, protocol_context_};
  api_context->Enter(
      kDefaultCryptoLibProfile,

      SubApi{[this, proofs = std::move(proofs)](
                 ApiContext<ServerRegistrationApi>& server_api) mutable {
        server_api->RegistrationDirect(pow_params_.salt,
                                       pow_params_.password_suffix,
                                       std::move(proofs), parent_uid_,
                                       master_key_);
      }});

  root_server_select_stream_.Write(DataBuffer{std::move(api_context)});
}





void Registration::ResolveCloud() {
  AE_TELE_INFO(RegisterResolveCloud, "Resolve cloud as [{}]", client_cloud_);
  auto api_context =
      ApiContext<RegistrationRootApi>{server_reg_root_api_, protocol_context_};
  api_context->Enter(kDefaultCryptoLibProfile,
                     SubApi{[this](ApiContext<ServerRegistrationApi>& server_api) {
                       server_api->ResolveServers(client_cloud_);
                     }});
  root_server_select_stream_.Write(DataBuffer{std::move(api_context)});
}



ClientConfig Registration::OnCloudResolved(
    std::vector<ServerDescriptor> const& servers) {
  AE_TELE_INFO(RegisterResolveCloudResponse);

  ClientConfig client_config{
      .parent_uid = parent_uid_,
      .uid = client_uid_,
      .ephemeral_uid = ephemeral_uid_,
      .master_key = master_key_,
      .cloud = {},
  };

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
    client_config.cloud.emplace_back(
        ServerConfig{description.server_id, std::move(endpoints)});
  }

  // sort cloud by order of client_cloud_
  std::sort(std::begin(client_config.cloud), std::end(client_config.cloud),
            [this](auto const& left, auto const& right) {
              auto left_order = std::find(std::begin(client_cloud_),
                                          std::end(client_cloud_), left.id);
              auto right_order = std::find(std::begin(client_cloud_),
                                           std::end(client_cloud_), right.id);
              return left_order < right_order;
            });

  AE_TELE_DEBUG(RegisterClientRegistered,
                "Client registered:\n>>>>>>>>>>>>>>>\n{}\n<<<<<<<<<<<<<<<",
                client_config);

  return client_config;
}



void Registration::Run() {
  GetKeys();
  RequestPowParams();
  MakeRegistration();
  ResolveCloud();
}
}  // namespace ae

#endif