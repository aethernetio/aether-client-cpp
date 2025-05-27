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
#include <vector>
#include <cstdint>

#include "aether/aether.h"
#include "aether/common.h"

#include "aether/ptr/ptr.h"
#include "aether/global_ids.h"
#include "aether/ae_actions/registration/registration.h"
#include "aether/client_messages/p2p_message_stream.h"

#include "aether/port/file_systems/file_system_header.h"

#include "aether/port/tele_init.h"
#include "aether/tele/tele.h"

#include "registrator/register_wifi.h"
#include "registrator/registrator_action.h"
#include "registrator/registrator_config.h"

int AetherRegistrator(const std::string& ini_file,
                      const std::string& header_file);

int AetherRegistrator(const std::string& ini_file,
                      const std::string& header_file) {
  ae::TeleInit::Init();

  ae::RegistratorConfig registrator_config{ini_file};
  auto res = registrator_config.ParseConfig();
  if (res < 0) {
    AE_TELED_ERROR("Configuration failed.");
    return -1;
  }

  /**
   * Construct a main aether application class.
   * It's include a Domain and Aether instances accessible by getter methods.
   * It has Update, WaitUntil, Exit, IsExit, ExitCode methods to integrate it in
   * your update loop.
   * Also it has action context protocol implementation \see Action.
   * To configure its creation \see AetherAppConstructor.
   */
  auto aether_app = ae::AetherApp::Construct(
      ae::AetherAppConstructor{[header_file]() {
        return ae::make_unique<ae::FileSystemHeaderFacility>(header_file);
      }}
#if defined AE_DISTILLATION
          .Adapter([&registrator_config](
                       ae::Domain* domain,
                       ae::Aether::ptr const& aether) -> ae::Adapter::ptr {
            if (registrator_config.GetWiFiIsSet()) {
              AE_TELED_DEBUG("ae::registrator::RegisterWifiAdapter");
              auto adapter =
                  domain->CreateObj<ae::registrator::RegisterWifiAdapter>(
                      ae::GlobalId::kRegisterWifiAdapter, aether,
                      aether->poller, registrator_config.GetWiFiSsid(),
                      registrator_config.GetWiFiPass());
              return adapter;
            } else {
              AE_TELED_DEBUG("ae::EthernetAdapter");
              auto adapter = domain->CreateObj<ae::EthernetAdapter>(
                  ae::GlobalId::kEthernetAdapter, aether, aether->poller);
              return adapter;
            }
          })
#  if AE_SUPPORT_REGISTRATION
          .RegCloud([&registrator_config](ae::Domain* domain,
                                          ae::Aether::ptr const& /* aether */) {
            auto registration_cloud = domain->CreateObj<ae::RegistrationCloud>(
                ae::kRegistrationCloud);
            auto servers_list = registrator_config.GetServers();
            for (auto s : servers_list) {
              AE_TELED_DEBUG("Server address type={}", s.server_address_type);
              AE_TELED_DEBUG("Server ip address version={}",
                             s.server_ip_address_version);
              AE_TELED_DEBUG("Server address={}", s.server_address);
              AE_TELED_DEBUG("Server port={}", s.server_port);
              AE_TELED_DEBUG("Server protocol={}", s.server_protocol);

              if (s.server_address_type == ae::ServerAddressType::kIpAddress) {
                ae::IpAddressPortProtocol settings{
                    {ae::IpAddress{s.server_ip_address_version, {}},
                     s.server_port},
                    s.server_protocol};
                if (s.server_ip_address_version ==
                    ae::IpAddress::Version::kIpV4) {
                  for (std::size_t i{0}; i < 4; i++) {
                    settings.ip.value.ipv4_value[i] =
                        s.server_ip_adress.value.ipv4_value[i];
                  }
                } else if (s.server_ip_address_version ==
                           ae::IpAddress::Version::kIpV6) {
                  for (std::size_t i{0}; i < 16; i++) {
                    settings.ip.value.ipv6_value[i] =
                        s.server_ip_adress.value.ipv6_value[i];
                  }
                }
                registration_cloud->AddServerSettings(settings);
              } else if (s.server_address_type ==
                         ae::ServerAddressType::kUrlAddress) {
                registration_cloud->AddServerSettings(ae::NameAddress{
                    s.server_address, s.server_port, s.server_protocol});
              }
            }

            return registration_cloud;
          })
#  endif  // AE_SUPPORT_REGISTRATION
#endif
  );

  auto registrator_action =
      ae::registrator::RegistratorAction{aether_app, registrator_config};

  auto success = registrator_action.ResultEvent().Subscribe(
      [&](auto const&) { aether_app->Exit(0); });
  auto failed = registrator_action.ErrorEvent().Subscribe(
      [&](auto const&) { aether_app->Exit(1); });

  while (!aether_app->IsExited()) {
    auto current_time = ae::Now();
    auto next_time = aether_app->Update(current_time);
    aether_app->WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
  }

  return aether_app->ExitCode();
}
