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

#include "aether/aether_app.h"

#include "aether/literal_array.h"

#include "aether/crypto.h"
#include "aether/crypto/key.h"
#include "aether/adapters/adapter_factory.h"

#include "aether/port/tele_init.h"
#include "aether/aether_tele.h"

namespace ae {
AetherAppConstructor::InitTele::InitTele() {
  ae::TeleInit::Init();
  AE_TELE_ENV();
  AE_TELE_INFO(Started);
  ae::Registry::Log();
}

Ptr<AetherApp> AetherApp::Construct(AetherAppConstructor&& constructor) {
  auto app = MakePtr<AetherApp>();
  app->domain_facility_ = std::move(constructor.domain_facility_);
  app->domain_ = std::move(constructor.domain_);
  app->aether_ = std::move(constructor.aether_);

  ae::TeleInit::Init(app->aether_);

#if defined AE_DISTILLATION
  auto adapter = constructor.adapter_
                     ? std::move(constructor.adapter_)
                     : AdapterFactory::Create(app->domain_, app->aether_);
  adapter.SetFlags(ae::ObjFlags::kUnloadedByDefault);
  app->aether_->adapter_factories.emplace_back(adapter);
  app->aether_->cloud_prefab->set_adapter(adapter);

#  if AE_SUPPORT_REGISTRATION
  auto reg_cloud =
      constructor.registration_cloud_
          ? std::move(constructor.registration_cloud_)
          : [&]() {
              auto reg_c = app->domain_->CreateObj<RegistrationCloud>(
                  kRegistrationCloud);
#    if !AE_SUPPORT_CLOUD_DNS
              reg_c->AddServerSettings(IpAddressPortProtocol{
                  {IpAddress{IpAddress::Version::kIpV4, {{34, 60, 244, 148}}},
                   9010},
                  Protocol::kTcp});
#    else
              // in case of ip address change
              reg_c->AddServerSettings(
                  NameAddress{"registration.aethernet.io", 9010});
#    endif
              return reg_c;
            }();
  app->aether_->registration_cloud = std::move(reg_cloud);
  app->aether_->registration_cloud->set_adapter(adapter);
#  endif  // AE_SUPPORT_REGISTRATION

  if (constructor.signed_keys_.empty()) {
#  if AE_SIGNATURE == AE_ED25519
    app->aether_->crypto->signs_pk_[ae::SignatureMethod::kEd25519] =
        ae::SodiumSignPublicKey{
            ae::MakeLiteralArray("4F202A94AB729FE9B381613AE77A8A7D89EDAB9299C33"
                                 "20D1A0B994BA710CCEB")};
#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
    app->aether_->crypto->signs_pk_[ae::SignatureMethod::kHydroSignature] =
        ae::HydrogenSignPublicKey{
            ae::MakeLiteralArray("883B4D7E0FB04A38CA12B3A451B00942048858263EE6E"
                                 "6D61150F2EF15F40343")};
#  endif  // AE_SIGNATURE == AE_ED25519
  } else {
    app->aether_->crypto->signs_pk_ = std::move(constructor.signed_keys_);
  }

  app->domain_->SaveRoot(app->aether_);
#endif  // defined AE_DISTILLATION
  return app;
}

AetherApp::~AetherApp() {
  // save aether_ state on exit
  if (domain_ && aether_) {
    domain_->SaveRoot(aether_);
  }
}

}  // namespace ae
