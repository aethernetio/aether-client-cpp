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
#include <tuple>
#include <vector>

#include "third_party/ini.h/ini.h"

#include "aether/aether.h"
#include "aether/ae_actions/registration/registration.h"
#include "aether/common.h"
#include "aether/global_ids.h"
#include "aether/port/tele_init.h"
#include "aether/tele/tele.h"
#include "aether/literal_array.h"

#include "aether/port/file_systems/file_system_header.h"
#include "aether/adapters/register_wifi.h"

constexpr std::uint8_t clients_max = 16;

namespace ae {
#if defined AE_DISTILLATION
static Aether::ptr CreateAetherInstrument(Domain& domain, std::string wifi_ssid,
                                          std::string wifi_pass) {
  Aether::ptr aether = domain.CreateObj<ae::Aether>(GlobalId::kAether);

  RegisterWifiAdapter::ptr adapter = domain.CreateObj<RegisterWifiAdapter>(
      GlobalId::kEsp32WiFiAdapter, aether, aether->poller,
      std::string(wifi_ssid), std::string(wifi_pass));
  adapter.SetFlags(ae::ObjFlags::kUnloadedByDefault);

  aether->adapter_factories.emplace_back(std::move(adapter));

#  if AE_SUPPORT_REGISTRATION
  // localhost
  aether->registration_cloud->AddServerSettings(IpAddressPortProtocol{
      {IpAddress{IpAddress::Version::kIpV4, {127, 0, 0, 1}}, 9010},
      Protocol::kTcp});*/
  // cloud address
  aether->registration_cloud->AddServerSettings(IpAddressPortProtocol{
      {IpAddress{IpAddress::Version::kIpV4, {35, 224, 1, 127}}, 9010},
      Protocol::kTcp});
  // cloud name address
  aether->registration_cloud->AddServerSettings(
      NameAddress{"registration.aethernet.io", 9010, Protocol::kTcp});
#  endif  // AE_SUPPORT_REGISTRATION
  return aether;
}
#endif

static Aether::ptr LoadAether(Domain& domain) {
  Aether::ptr aether;
  aether.SetId(GlobalId::kAether);
  domain.LoadRoot(aether);
  assert(aether);
  return aether;
}

}  // namespace ae

int AetherRegistrator(const std::string& ini_file);

int AetherRegistrator(const std::string& ini_file) {
  ae::TeleInit::Init();

  {
    AE_TELE_ENV();
    AE_TELE_INFO("Started");
    ae::Registry::Log();
  }

  // Reading settings from the ini file.
  ini::File file = ini::open(ini_file);

  std::string wifi_ssid = file["Aether"]["wifiSsid"];
  std::string wifi_pass = file["Aether"]["wifiPass"];

  AE_TELED_DEBUG("WiFi ssid={}", wifi_ssid);
  AE_TELED_DEBUG("WiFi pass={}", wifi_pass);

  std::string sodium_key = file["Aether"]["sodiumKey"];
  std::string hydrogen_key = file["Aether"]["hydrogenKey"];

  AE_TELED_DEBUG("Sodium key={}", sodium_key);
  AE_TELED_DEBUG("Hydrogen key={}", hydrogen_key);

  std::int8_t parents_num = file["Aether"].get<int>("parentsNum");

  std::string uid;
  std::uint8_t clients_num, clients_total{0};
  std::tuple<std::string, std::uint8_t> parent;
  std::vector<std::tuple<std::string, std::int8_t>> parents;

  for (std::uint8_t i{0}; i < parents_num; i++) {
    uid = file["ParentID" + std::to_string(i + 1)]["uid"];
    clients_num =
        file["ParentID" + std::to_string(i + 1)].get<int>("clientsNum");
    parent = make_tuple(uid, clients_num);
    parents.push_back(parent);
    clients_total += clients_num;
  }

  // Clients max assertion
  if (clients_total > clients_max) {
    std::cerr << "Total clients must be < " << clients_max << " clients\n";
    return -1;
  }

  auto fs = ae::FileSystemHeaderFacility{};

#ifdef AE_DISTILLATION
  // create objects in instrument mode
  {
    ae::Domain domain{ae::ClockType::now(), fs};
    fs.remove_all();
    ae::Aether::ptr aether =
        ae::CreateAetherInstrument(domain, wifi_ssid, wifi_pass);
#  if AE_SIGNATURE == AE_ED25519
    auto sspk_str = sodium_key;
    auto sspk_arr = ae::MakeArray(sspk_str);
    auto sspk = ae::SodiumSignPublicKey{};
    if (sspk_arr.size() != sspk.key.size()) {
      std::cerr << "SodiumSignPublicKey size must be " << sspk.key.size()
                << " bytes\n";
      return -2;
    }

    std::copy(std::begin(sspk_arr), std::end(sspk_arr), std::begin(sspk.key));
    aether->crypto->signs_pk_[ae::SignatureMethod::kEd25519] = sspk;
#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
    auto hspk_str = hydrogen_key;
    auto hspk_arr = ae::MakeArray(hspk_str);
    auto hspk = ae::HydrogenSignPublicKey{};
    if (hspk_arr.size() != hspk.key.size()) {
      std::cerr << "HydrogenSignPublicKey size must be " << hspk.key.size()
                << " bytes\n";
      return -2;
    }

    std::copy(std::begin(hspk_arr), std::end(hspk_arr), std::begin(hspk.key));
    aether->crypto->signs_pk_[ae::SignatureMethod::kHydroSignature] = hspk;
#  endif  // AE_SIGNATURE == AE_ED25519
    domain.SaveRoot(aether);
  }
#endif  // AE_DISTILLATION

  ae::Domain domain{ae::ClockType::now(), fs};
  ae::Aether::ptr aether = ae::LoadAether(domain);
  ae::TeleInit::Init(aether);

  ae::Adapter::ptr adapter{domain.LoadCopy(aether->adapter_factories.front())};

  ae::Client::ptr client;

  // Creating the actual adapter.
  auto& cloud = aether->registration_cloud;
  domain.LoadRoot(cloud);
  cloud->set_adapter(adapter);

  for (auto p : parents) {
    std::string uid_str = std::get<0>(p);
    std::uint8_t clients_num = std::get<1>(p);
    for (std::uint8_t i{0}; i < clients_num; i++) {
#if AE_SUPPORT_REGISTRATION
      auto uid_arr = ae::MakeArray(uid_str);
      if (uid_arr.size() != ae::Uid::kSize) {
        std::cerr << "Uid size must be " << ae::Uid::kSize << " bytes\n";
        return -3;
      }
      auto uid = ae::Uid{};
      std::copy(std::begin(uid_arr), std::end(uid_arr), std::begin(uid.value));
      auto registration = aether->RegisterClient(uid);

      bool register_done = false;
      bool register_failed = false;

      auto reg = registration->SubscribeOnResult([&](auto const& reg) {
        register_done = true;
        client = reg.client();
      });
      auto reg_failed = registration->SubscribeOnError(
          [&](auto const&) { register_failed = true; });

      while (!register_done && !register_failed) {
        auto time = ae::TimePoint::clock::now();
        auto next_time = domain.Update(time);
        aether->action_processor->get_trigger().WaitUntil(next_time);

        if (register_failed) {
          std::cerr << "Registration failed\n";
          return -4;
        }
      }
#endif
    }
  }

  // save objects state
  domain.SaveRoot(aether);

  return 0;
}