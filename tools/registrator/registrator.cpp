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
#include "aether/wifi/wifi_driver_types.h"

#include "registrator/register_wifi.h"
#include "registrator/registrator_action.h"
#include "registrator/registrator_config.h"

int AetherRegistrator(const std::string& ini_file,
                      const std::string& header_file) {
  // Init tlemetry
  auto io_trap = std::make_shared<ae::tele::IoStreamTrap>(std::cout);
  TELE_SINK::Instance().SetTrap(io_trap);

#if AE_SUPPORT_REGISTRATION
  // get registrator config
  ae::reg::RegistratorConfig registrator_config{ini_file};
  if (!registrator_config.aether()) {
    std::cerr << "Aether section in config is required" << std::endl;
    return -1;
  }

  auto construct_context =
      ae::AetherAppContext{
          // make special domain storage to write all state into header file
          [&header_file]() {
            return ae::make_unique<ae::RegistrarDomainStorage>(header_file);
          },
          // empty tele initializer
          [](auto const&) {},
      }
          .CryptoFactory([&registrator_config](
                             ae::AetherAppContext const& context) {
            auto crypto = ae::Crypto::ptr::Create(
                ae::CreateWith{context.domain()}.with_id(
                    ae::GlobalId::kCrypto));
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
          .RegistrationCloudFactory(
              [&registrator_config](ae::AetherAppContext const& context) {
                auto registration_cloud = ae::RegistrationCloud::ptr::Create(
                    ae::CreateWith{context.domain()}.with_id(
                        ae::GlobalId::kRegistrationCloud),
                    context.aether());

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
              });

  if (auto const& wifi = registrator_config.wifi_adapter(); wifi) {
#  if AE_SUPPORT_WIFIS
    construct_context =
        std::move(construct_context)
            .AddAdapterFactory([&wifi](ae::AetherAppContext const& context) {
              AE_TELED_DEBUG("ae::reg::RegisterWifiAdapter");

              // TODO: why this values?
              static ae::IpV4Addr my_static_ip_v4{192, 168, 1, 215};
              static ae::IpV4Addr my_gateway_ip_v4{192, 168, 1, 1};
              static ae::IpV4Addr my_netmask_ip_v4{255, 255, 255, 0};
              static ae::IpV4Addr my_dns1_ip_v4{8, 8, 8, 8};
              static ae::IpV4Addr my_dns2_ip_v4{8, 8, 4, 4};
              static ae::IpV6Addr my_static_ip_v6{
                  0x20, 0x01, 0x0d, 0xb8, 0x85, 0xa3, 0x00, 0x00,
                  0x00, 0x00, 0x8a, 0x2e, 0x03, 0x70, 0x73, 0x34};

              static ae::WiFiIP wifi_ip{
                  {my_static_ip_v4},   // ESP32 static IP
                  {my_gateway_ip_v4},  // IP Address of your network
                                       // gateway (router)
                  {my_netmask_ip_v4},  // Subnet mask
                  {my_dns1_ip_v4},     // Primary DNS (optional)
                  {my_dns2_ip_v4},     // Secondary DNS (optional)
                  {my_static_ip_v6}    // ESP32 static IP v6
              };

              ae::WifiCreds creds{wifi->ssid, wifi->password};

              ae::WiFiAp wifi_ap{creds, wifi_ip};

              std::vector<ae::WiFiAp> wifi_ap_vec{wifi_ap};

              static ae::WiFiPowerSaveParam wifi_psp{
                  true,
                  AE_WIFI_PS_MAX_MODEM,  // Power save type
                  AE_WIFI_PROTOCOL_11B | AE_WIFI_PROTOCOL_11G |
                      AE_WIFI_PROTOCOL_11N,  // Protocol bitmap
                  3,                         // Listen interval
                  500                        // Beacon interval
              };

              ae::WiFiInit wifi_init{
                  wifi_ap_vec,  // Wi-Fi access points
                  wifi_psp,     // Power save parameters
              };

              return ae::reg::RegisterWifiAdapter::ptr::Create(
                  ae::CreateWith{context.domain()}.with_id(
                      ae::GlobalId::kRegisterWifiAdapter),
                  context.aether(), context.poller(), context.dns_resolver(),
                  wifi_init);
            });
#  endif
  } else {
    construct_context =
        std::move(construct_context)
            .AddAdapterFactory([](ae::AetherAppContext const& context) {
              AE_TELED_DEBUG("ae::EthernetAdapter");
              return ae::EthernetAdapter::ptr::Create(
                  ae::CreateWith{context.domain()}.with_id(
                      ae::GlobalId::kEthernetAdapter),
                  context.aether(), context.poller(), context.dns_resolver());
            });
  }

  auto aether_app = ae::AetherApp::Construct(std::move(construct_context));

  if (registrator_config.clients().empty()) {
    std::cout << "No client configurations provided! Exit!" << std::endl;
    return 0;
  }

  auto registrator_action = ae::ActionPtr<ae::reg::RegistratorAction>{
      *aether_app, aether_app, registrator_config.clients()};

  registrator_action->StatusEvent().Subscribe(ae::ActionHandler{
      ae::OnResult{[&](auto const& action) {
        auto const& clients = action.registered_clients();
        for (auto const& c_conf : clients) {
          std::cout << ae::Format("\nclient={},\n{}\n", c_conf.client_id,
                                  c_conf.config);
          aether_app->aether()->CreateClient(c_conf.config, c_conf.client_id);
        }
        aether_app->Exit(0);
      }},
      ae::OnError{[&]() { aether_app->Exit(1); }},
  });

  while (!aether_app->IsExited()) {
    auto current_time = ae::Now();
    auto next_time = aether_app->Update(current_time);
    aether_app->WaitUntil(next_time);
  }

  return aether_app->ExitCode();
#else
  AE_TELED_ERROR("AE_SUPPORT_REGISTRATION is not supported");
  assert(false);
  return -3;
#endif
}
