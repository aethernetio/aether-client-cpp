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
  ae::RegistratorConfig registrator_config{ini_file};
  auto res = registrator_config.ParseConfig();
  if (res < 0) {
    AE_TELED_ERROR("Configuration failed.");
    return -1;
  }

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
          .AdaptersFactory([&registrator_config](
                               ae::AetherAppContext const& context) {
            auto adapter_registry =
                std::make_shared<ae::AdapterRegistry>(context.domain());

            if (registrator_config.GetWiFiIsSet()) {
              AE_TELED_DEBUG("ae::registrator::RegisterWifiAdapter");
              adapter_registry->Add(
                  std::make_shared<ae::registrator::RegisterWifiAdapter>(
                      *context.aether(), *context.poller(),
                      *context.dns_resolver(), registrator_config.GetWiFiSsid(),
                      registrator_config.GetWiFiPass(), context.domain()));
            } else {
              AE_TELED_DEBUG("ae::EthernetAdapter");
              adapter_registry->Add(std::make_shared<ae::EthernetAdapter>(
                  *context.aether(), *context.poller(), *context.dns_resolver(),
                  context.domain()));
            }
            return adapter_registry;
          })
          .RegistrationCloudFactory([&registrator_config](
                                        ae::AetherAppContext const& context) {
            auto registration_cloud = std::make_shared<ae::RegistrationCloud>(
                *context.aether(), context.domain());

            auto servers_list = registrator_config.GetServers();
            for (auto s : servers_list) {
              AE_TELED_DEBUG("Server address={}", s.server_ip_address);
              AE_TELED_DEBUG("Server port={}", s.server_port);
              AE_TELED_DEBUG("Server protocol={}", s.server_protocol);

              ae::Endpoint settings{{s.server_ip_address, s.server_port},
                                    s.server_protocol};
              registration_cloud->AddServerSettings(settings);
            }
            return registration_cloud;
          }));

  auto registrator_action = ae::ActionPtr<ae::registrator::RegistratorAction>{
      *aether_app, aether_app, registrator_config};

  registrator_action->StatusEvent().Subscribe(
      ae::ActionHandler{ae::OnResult{[&]() { aether_app->Exit(0); }},
                        ae::OnError{[&]() { aether_app->Exit(1); }}});

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
