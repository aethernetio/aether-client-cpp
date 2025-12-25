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

#include <string>

#include "aether/all.h"
#include "aether/crypto.h"
#include "aether/domain_storage/registrar_domain_storage.h"

#include "registrator/register_wifi.h"
#include "registrator/registrator_action.h"
#include "registrator/registrator_config.h"

int AetherRegistrator(const std::string& ini_file,
                      const std::string& header_file);

int AetherRegistrator(const std::string& ini_file,
                      const std::string& header_file) {
  // Init tlemetry
  auto io_trap = std::make_shared<ae::tele::IoStreamTrap>(std::cout);
  TELE_SINK::Instance().SetTrap(io_trap);

#if AE_SUPPORT_REGISTRATION
  // get registrator config
  ae::reg::RegistratorConfig registrator_config{ini_file};
  assert(registrator_config.aether() && "Aether config is required");

  /**
   * Construct a main aether application class.
   * It's include a Domain and Aether instances accessible by getter
   * methods. It has Update, WaitUntil, Exit, IsExit, ExitCode methods to
   * integrate it in your update loop. Also it has action context protocol
   * implementation \see Action. To configure its creation \see
   * AetherAppConstructor.
   */
  auto aether_app = ae::AetherApp::Construct(
      ae::AetherAppContext{
          // make special domain storage to write all state into header file
          [header_file]() {
            return ae::make_unique<ae::RegistrarDomainStorage>(header_file);
          },
          // empty tele initializer
          [](auto const&) {}}
          .CryptoFactory([&registrator_config](
                             ae::AetherAppContext const& context) {
            auto crypto = context.domain().CreateObj<ae::Crypto>();
#  if AE_SIGNATURE == AE_ED25519
            assert((registrator_config.aether()->ed25519_sign_key.size() / 2 ==
                    crypto_sign_PUBLICKEYBYTES) &&
                   "Ed25519 key size mismatch");
            crypto->signs_pk_[ae::SignatureMethod::kEd25519] =
                ae::SodiumSignPublicKey{
                    ae::MakeArray<crypto_sign_PUBLICKEYBYTES>(
                        registrator_config.aether()->ed25519_sign_key)};
#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
            assert((registrator_config.aether()->hydrogen_sign_key.size() / 2 ==
                    hydro_sign_PUBLICKEYBYTES) &&
                   "Hydrogen key size mismatch");
            crypto->signs_pk_[ae::SignatureMethod::kHydroSignature] =
                ae::HydrogenSignPublicKey{
                    ae::MakeArray<hydro_sign_PUBLICKEYBYTES>(std::string_view{
                        registrator_config.aether()->hydrogen_sign_key})};
#  endif  // AE_SIGNATURE
            return crypto;
          })
          .AdaptersFactory([&registrator_config](
                               ae::AetherAppContext const& context)
                               -> ae::AdapterRegistry::ptr {
            auto adapter_registry =
                context.domain().CreateObj<ae::AdapterRegistry>();

            if (auto wifi = registrator_config.wifi_adapter(); wifi) {
              AE_TELED_DEBUG("ae::registrator::RegisterWifiAdapter");
              adapter_registry->Add(
                  context.domain()
                      .CreateObj<ae::registrator::RegisterWifiAdapter>(
                          ae::GlobalId::kRegisterWifiAdapter, context.aether(),
                          context.poller(), context.dns_resolver(), wifi->ssid,
                          wifi->password));
            } else {
              AE_TELED_DEBUG("ae::EthernetAdapter");
              adapter_registry->Add(
                  context.domain().CreateObj<ae::EthernetAdapter>(
                      ae::GlobalId::kEthernetAdapter, context.aether(),
                      context.poller(), context.dns_resolver()));
            }
            return adapter_registry;
          })
          .RegistrationCloudFactory(
              [&registrator_config](ae::AetherAppContext const& context) {
                auto registration_cloud =
                    context.domain().CreateObj<ae::RegistrationCloud>(
                        ae::GlobalId::kRegistrationCloud, context.aether());

                auto servers_list = registrator_config.reg_servers();
                for (auto const& s : servers_list) {
                  AE_TELED_DEBUG("Server address={}, port={}, protocol={}",
                                 s.address, s.port, s.protocol);
                  ae::Endpoint endpoint{
                      {ae::AddressParser::StringToAddress(s.address), s.port},
                      s.protocol,
                  };
                  registration_cloud->AddServerSettings(std::move(endpoint));
                }
                return registration_cloud;
              }));

  auto registrator_action = ae::ActionPtr<ae::registrator::RegistratorAction>{
      *aether_app, aether_app, registrator_config.clients()};

  registrator_action->StatusEvent().Subscribe(ae::ActionHandler{
      ae::OnResult{[&]() { aether_app->Exit(0); }},
      ae::OnError{[&]() { aether_app->Exit(1); }},
  });

  while (!aether_app->IsExited()) {
    auto current_time = ae::Now();
    auto next_time = aether_app->Update(current_time);
    aether_app->WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
  }

  return aether_app->ExitCode();
#else
  AE_TELED_ERROR("AE_SUPPORT_REGISTRATION is not supported");
  assert(false);
  return -3;
#endif
}
