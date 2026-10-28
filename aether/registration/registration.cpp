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
      client_root_api_{*root_crypto_provider_.decryptor(),
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
  Run();
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


auto Registration::GetKeys() {
  AE_TELE_INFO(RegisterGetKeys, "GetKeys");
  server_reg_root_api_.GetPublicKey(kDefaultCryptoLibProfile);
  return ex::just_value();
}



auto Registration::RequestPowParams() {
  AE_TELE_INFO(RegisterRequestPowParams, "Request proof of work params");
  server_reg_root_api_.Enter(
      kDefaultCryptoLibProfile,
      SubApi{[this](ApiContext<ServerRegistrationApi>& server_api) {
        server_api->SetReturnKey(root_crypto_provider_.GetDecryptionKey());
        server_api->RequestProofOfWorkData(parent_uid_, PowMethod::kBCryptCrc32);
      }});
  return ex::just_value();
}



auto Registration::MakeRegistration() {
  AE_TELE_INFO(RegisterMakeRegistration, "Make registration");

  auto proofs = ProofOfWork::ComputeProofOfWork(
      pow_params_.pool_size, pow_params_.salt, pow_params_.password_suffix,
      pow_params_.max_hash_value);

  AE_TELE_DEBUG(RegisterKeysGenerated, "Global Pk: {}:{}, Masterkey: {}:{}",
                aether_global_key_.Index(), aether_global_key_, master_key_.Index(),
                master_key_);

  server_reg_root_api_.Enter(
      kDefaultCryptoLibProfile,
      SubApi{[this, proofs =
                        std::move(proofs)](ApiContext<ServerRegistrationApi>& server_api) mutable {
        server_api->Registration(
            pow_params_.salt, pow_params_.password_suffix, std::move(proofs),
            parent_uid_, [this](ApiContext<GlobalRegServerApi>& global_api) {
              global_api->SetMasterKey(master_key_);
              global_api->Finish();
            });
      }});
  return ex::just_value();
}



auto Registration::ResolveCloud() {
  AE_TELE_INFO(RegisterResolveCloud, "Resolve cloud as [{}]", client_cloud_);
  server_reg_root_api_.Enter(kDefaultCryptoLibProfile,
      [this](ApiContext<ServerRegistrationApi>& server_api) noexcept {
        server_api->ResolveServers(client_cloud_);
      });
  return ex::just_value();
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


  waiter_.emplace(ae_context_, std::move(s),
                  [&](std::optional<Result<ClientConfig, int>>&& res) noexcept {
                    assert(res);
                    registration_event_.Emit(std::move(res).value());
                    Finish();
                  });
}
}  // namespace ae
#endif