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
#  include "aether/crypto.h"
#  include "aether/crypto/sign.h"
#  include "aether/misc/override.h"
#  include "aether/executors/executors.h"
#  include "aether/api_protocol/api_context.h"
#  include "aether/crypto/crypto_definitions.h"
#  include "aether/api_protocol/make_api_call_sender.h"

#  include "aether/registration/proof_of_work.h"
#  include "aether/registration/registration_crypto_provider.h"

#  include "aether/ae_actions/ae_actions_tele.h"

namespace ae {

Registration::Registration(AeContext const& ae_context,
                           Ptr<RegistrationCloud> const& reg_cloud,
                           Uid parent_uid)
    : ae_context_{ae_context},
      parent_uid_{std::move(parent_uid)},
      root_crypto_provider_{},
      global_crypto_provider_{},
      client_root_api_{protocol_context_, *root_crypto_provider_.decryptor(),
                       *global_crypto_provider_.decryptor()},
      server_reg_root_api_{protocol_context_,
                           *root_crypto_provider_.encryptor(),
                           *global_crypto_provider_.encryptor()},
      root_server_select_stream_{ae_context_, reg_cloud},
      // TODO: add configuration
      response_timeout_{std::chrono::seconds(20)},
      sign_pk_{Crypto::ptr{ae_context_.aether().crypto}
                   ->signs_pk_[kDefaultSignatureMethod]} {
  AE_TELE_INFO(RegisterStarted);

  // parent uid must not be empty
  assert(!parent_uid_.empty());

  InitConnection();
  Run();
}

Registration::~Registration() { AE_TELED_DEBUG("~Registration"); }

Registration::RegistrationEvent::Subscriber Registration::registration() {
  return EventSubscriber{registration_event_};
}
Registration::FinishedEvent::Subscriber Registration::IsFinished() {
  return EventSubscriber{finished_event_};
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
  return make_api_call<ex::set_value_t(Ignore), ex::set_error_t(std::uint32_t)>(
      server_reg_root_api_, root_server_select_stream_,
      [&, s{Subscription{}}](ApiContext<RegistrationRootApi>& api_call,
                             auto& r) mutable noexcept {
        AE_TELE_INFO(RegisterGetKeys, "GetKeys");
        s = api_call->get_asymmetric_public_key(kDefaultCryptoLibProfile)
                .Subscribe([&](auto&& res) mutable noexcept {
                  if (!res) {
                    ex::set_error(std::move(r), std::move(res).error());
                    return;
                  }
                  auto signed_key = std::move(res).value();
                  if (!CryptoSignVerify(signed_key.sign, signed_key.key,
                                        sign_pk_)) {
                    AE_TELE_ERROR(RegisterGetKeysVerificationFailed,
                                  "Sign verification failed");
                    ex::set_error(std::move(r), 1);
                    return;
                  }
                  server_pub_key_ = std::move(signed_key.key);
                  root_crypto_provider_.SetEncryptionKey(server_pub_key_);
                  AE_TELED_INFO("Key received");
                  ex::set_value(std::move(r), Ignore{});
                });
      });
}

auto Registration::RequestPowParams() {
  return make_api_call<ex::set_value_t(Ignore), ex::set_error_t(std::uint32_t)>(
      server_reg_root_api_, root_server_select_stream_,
      [&, s{Subscription{}}](ApiContext<RegistrationRootApi>& api_call,
                             auto& r) mutable noexcept {
        AE_TELE_INFO(RegisterRequestPowParams, "Request proof of work params");
        api_call->enter(
            kDefaultCryptoLibProfile,
            SubApi{[&](ApiContext<ServerRegistrationApi>& server_api) {
              server_api->set_return_key(
                  root_crypto_provider_.GetDecryptionKey());
              s = server_api
                      ->request_proof_of_work_data(parent_uid_,
                                                   PowMethod::kBCryptCrc32)
                      .Subscribe([&](auto&& res) mutable noexcept {
                        if (!res) {
                          ex::set_error(std::move(r), std::move(res).error());
                          return;
                        }

                        auto pow_params = std::move(res).value();
                        if (!CryptoSignVerify(pow_params.global_key.sign,
                                              pow_params.global_key.key,
                                              sign_pk_)) {
                          AE_TELE_ERROR(
                              RegisterPowParamsVerificationFailed,
                              "Proof of work params sign verification failed");
                          ex::set_error(std::move(r), 2);
                          return;
                        }

                        aether_global_key_ = pow_params.global_key.key;
                        pow_params_.salt = pow_params.salt;
                        pow_params_.max_hash_value = pow_params.max_hash_value;
                        pow_params_.password_suffix =
                            pow_params.password_suffix;
                        pow_params_.pool_size = pow_params.pool_size;

                        global_crypto_provider_.SetEncryptionKey(
                            aether_global_key_);
                        master_key_ =
                            global_crypto_provider_.GetDecryptionKey();

                        ex::set_value(std::move(r), Ignore{});
                      });
            }});
      });
}

auto Registration::MakeRegistration() {
  return make_api_call<ex::set_value_t(Ignore), ex::set_error_t(std::uint32_t)>(
      server_reg_root_api_, root_server_select_stream_,
      [&, s{Subscription{}}](ApiContext<RegistrationRootApi>& api_call,
                             auto& r) mutable noexcept {
        AE_TELE_INFO(RegisterMakeRegistration, "Make registration");

        // Proof calculation
        auto proofs = ProofOfWork::ComputeProofOfWork(
            pow_params_.pool_size, pow_params_.salt,
            pow_params_.password_suffix, pow_params_.max_hash_value);

        AE_TELE_DEBUG(RegisterKeysGenerated,
                      "Global Pk: {}:{}, Masterkey: {}:{}",
                      aether_global_key_.Index(), aether_global_key_,
                      master_key_.Index(), master_key_);

        api_call->enter(
            kDefaultCryptoLibProfile,
            SubApi{[&](ApiContext<ServerRegistrationApi>& server_api) {
              server_api->registration(
                  pow_params_.salt, pow_params_.password_suffix, proofs,
                  parent_uid_,
                  SubApi{[&](ApiContext<GlobalRegServerApi>& global_api) {
                    // set master key and wait for confirmation
                    global_api->set_master_key(master_key_);
                    s = global_api->finish().Subscribe(
                        [&, r{std::forward<decltype(r)>(r)}](
                            auto&& res) mutable noexcept {
                          if (!res) {
                            ex::set_error(std::move(r), std::move(res).error());
                            return;
                          }
                          AE_TELE_INFO(RegisterRegistrationConfirmed,
                                       "Registration confirmed");
                          client_uid_ = res.value().uid;
                          ephemeral_uid_ = res.value().ephemeral_uid;
                          // use client cloud_ for cloud resolve request
                          client_cloud_ = res.value().cloud;
                          assert((client_cloud_.size() > 0) &&
                                 "Client's cloud is empty");
                          ex::set_value(std::move(r), Ignore{});
                        });
                  }});
            }});
      });
}

auto Registration::ResolveCloud() {
  return make_api_call<ex::set_value_t(ClientConfig),
                       ex::set_error_t(std::uint32_t)>(
      server_reg_root_api_, root_server_select_stream_,
      [&, s{Subscription{}}](ApiContext<RegistrationRootApi>& api_call,
                             auto& r) mutable noexcept {
        AE_TELE_INFO(RegisterResolveCloud, "Resolve cloud as [{}]",
                     client_cloud_);

        api_call->enter(
            kDefaultCryptoLibProfile,
            SubApi{[&](ApiContext<ServerRegistrationApi>&
                           server_api) mutable noexcept {
              s = server_api->resolve_servers(client_cloud_)
                      .Subscribe([&](auto&& res) {
                        if (res) {
                          ex::set_value(
                              std::move(r),
                              OnCloudResolved(std::move(res).value()));
                        } else {
                          ex::set_error(std::move(r), std::move(res).error());
                        }
                      });
            }});
      });
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
  auto s =
      // try to get keys a few times
      ex::for_range(Range{0, 4},
                    [&](auto) {
                      return GetKeys() |
                             ex::with_timeout(ae_context_, response_timeout_) |
                             ex::upon_error(Override{
                                 [&](ex::TimeoutError) {
                                   AE_TELE_WARNING(RegisterWaitKeysTimeout,
                                                   "WaitKeys timeout");
                                   Restream();
                                   return ex::for_continue;
                                 },
                                 [&](auto&&...) noexcept {
                                   Restream();
                                   return ex::for_continue;
                                 }});
                    }) |
      ex::let_stopped([&]() noexcept { return ex::just_error(1); }) |
      // perform the rest of the registration process
      ex::let_value([&](auto&&) noexcept { return RequestPowParams(); }) |
      ex::let_value([&](auto&&) noexcept { return MakeRegistration(); }) |
      ex::let_value([&](auto&&) noexcept { return ResolveCloud(); }) |
      ex::let_error(Override{
          [&](std::uint32_t e) noexcept {
            AE_TELED_ERROR("Registration failed with error code: {}", e);
            return ex::just_error(static_cast<int>(e));
          },
          [&](WriteFailed) noexcept {
            AE_TELED_ERROR("Registration failed with write error");
            return ex::just_error(-1);
          },
          [&](auto&&...) noexcept {
            AE_TELED_ERROR("Registration failed");
            return ex::just_error(-1);
          }});

  async_waiter_.emplace(
      ae_context_, std::move(s),
      [&](std::optional<Result<ClientConfig, int>>&& res) noexcept {
        assert(res);
        registration_event_.Emit(std::move(res).value());
        finished_event_.Emit();
      });
}
}  // namespace ae
#endif
