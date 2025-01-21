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

#include "third_party/ini.h/ini.h"

#include "aether/aether.h"
#include "aether/ae_actions/registration/registration.h"
#include "aether/common.h"
#include "aether/global_ids.h"
#include "aether/port/tele_init.h"
#include "aether/tele/tele.h"


namespace ae {
#if defined AE_DISTILLATION
static Aether::ptr CreateAetherInstrument(Domain& domain) {
  Aether::ptr aether = domain.CreateObj<ae::Aether>(GlobalId::kAether);


  RegisterWifiAdapter::ptr adapter = domain.CreateObj<RegisterWifiAdapter>(
      GlobalId::kEsp32WiFiAdapter, aether, aether->poller,
      std::string(WIFI_SSID), std::string(WIFI_PASS));
  adapter.SetFlags(ae::ObjFlags::kUnloadedByDefault);
  adapter->Connect();

  aether->adapter_factories.emplace_back(std::move(adapter));

#  if AE_SUPPORT_REGISTRATION
  // localhost
  aether->registration_cloud->AddServerSettings(IpAddressPortProtocol{
      {IpAddress{IpAddress::Version::kIpV4, {127, 0, 0, 1}}, 9010},
      Protocol::kTcp});
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

void AetherRegistrator(const std::string &ini_file);

void AetherRegistrator(const std::string &ini_file) {
  ae::TeleInit::Init();

  {
    AE_TELE_ENV();
    AE_TELE_INFO("Started");
    ae::Registry::Log();
  }

  ini::File file = ini::open(ini_file);

  std::string wifi_ssid = file["Aether"]["wifiSsid"];
  std::string wifi_pass = file["Aether"]["wifiPass"];

  AE_TELED_DEBUG("WiFi ssid={}", wifi_ssid);
  AE_TELED_DEBUG("WiFi pass={}", wifi_pass);

  std::string sodium_key = file["Aether"]["sodiumKey"];
  std::string hydrogen_key = file["Aether"]["hydrogenKey"];

  AE_TELED_DEBUG("Sodium key={}", sodium_key);
  AE_TELED_DEBUG("Hydrogen key={}", hydrogen_key);

    auto fs =
      ae::FileSystemHeaderFacility{};

#ifdef AE_DISTILLATION
  // create objects in instrument mode
  {
    ae::Domain domain{ae::ClockType::now(), fs};
    fs.remove_all();
    ae::Aether::ptr aether = ae::CreateAetherInstrument(domain);
#  if AE_SIGNATURE == AE_ED25519
    aether->crypto->signs_pk_[ae::SignatureMethod::kEd25519] =
        ae::SodiumSignPublicKey{
            ae::MakeLiteralArray(sodium_key)};

#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
    aether->crypto->signs_pk_[ae::SignatureMethod::kHydroSignature] =
        ae::HydrogenSignPublicKey{
            ae::MakeLiteralArray(hydrogen_key)};
#  endif  // AE_SIGNATURE == AE_ED25519
    domain.SaveRoot(aether);
  }
#endif  // AE_DISTILLATION
}