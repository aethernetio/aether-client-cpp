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

// IWYU pragma: begin_keeps
#include "aether/types/literal_array.h"
#include "aether/types/address_parser.h"

#include "aether/crypto.h"
#include "aether/crypto/key.h"
#include "aether/adapters/ethernet.h"
#include "aether/adapters/wifi_adapter.h"
#include "aether/poller/win_poller.h"
#include "aether/poller/epoll_poller.h"
#include "aether/poller/kqueue_poller.h"
#include "aether/poller/freertos_poller.h"

#include "aether/dns/dns_c_ares.h"
#include "aether/dns/esp32_dns_resolve.h"
// IWYU pragma: end_keeps

#include "aether/tele/tele_init.h"

#include "aether/aether_tele.h"

namespace ae {

AetherAppContext::TelemetryInit::TelemetryInit() {
  tele::TeleInit::Init();
  AE_TELE_ENV();
  AE_TELE_INFO(AetherStarted);
}

void AetherAppContext::TelemetryInit::operator()(
    AetherAppContext const& context) const {
  ae::tele::TeleInit::Init(context.aether()->tele_statistics);
}

void AetherAppContext::InitComponentContext() {
  Factory<AdapterRegistry>([](AetherAppContext const& context) {
    auto adapter_registry = std::make_shared<AdapterRegistry>(context.domain());
    adapter_registry->Add(std::make_shared<EthernetAdapter>(
        *context.aether(), *context.poller(), *context.dns_resolver(),
        context.domain()));
    return adapter_registry;
  });

#if AE_SUPPORT_REGISTRATION
  Factory<Cloud>([](AetherAppContext const& context) {
    auto reg_c = std::make_shared<RegistrationCloud>(*context.aether(),
                                                     context.domain());
#  if defined _AE_REG_CLOUD_IP
    reg_c->AddServerSettings(
        Endpoint{{AddressParser::StringToAddress(_AE_REG_CLOUD_IP), 9010},
                 Protocol::kTcp});
#  endif
#  if !AE_SUPPORT_CLOUD_DNS
    reg_c->AddServerSettings(
        Endpoint{{AddressParser::StringToAddress("34.60.244.148"), 9010},
                 Protocol::kTcp});
#  else
    // in case of ip address change
    reg_c->AddServerSettings(Endpoint{
        {AddressParser::StringToAddress("registration.aethernet.io"), 9010},
        Protocol::kTcp});

#  endif
    return reg_c;
  });
#endif  // AE_SUPPORT_REGISTRATION

  Factory<Crypto>([](AetherAppContext const& context) {
    auto crypto = std::make_shared<Crypto>(context.domain());
#if AE_SIGNATURE == AE_ED25519
    crypto->signs_pk_[ae::SignatureMethod::kEd25519] = ae::SodiumSignPublicKey{
        MakeArray("4F202A94AB729FE9B381613AE77A8A7D89EDAB9299C33"
                  "20D1A0B994BA710CCEB")};
#elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
    crypto->signs_pk_[ae::SignatureMethod::kHydroSignature] =
        ae::HydrogenSignPublicKey{
            MakeArray("883B4D7E0FB04A38CA12B3A451B00942048858263EE6E"
                      "6D61150F2EF15F40343")};
#endif  // AE_SIGNATURE == AE_ED25519
    return crypto;
  });

  Factory<IPoller>([](AetherAppContext const& context) {
    auto poller =
#if defined EPOLL_POLLER_ENABLED
        std::make_shared<EpollPoller>(context.domain());
#elif defined KQUEUE_POLLER_ENABLED
        std::make_shared<KqueuePoller>(context.domain());
#elif defined FREERTOS_POLLER_ENABLED
        std::make_shared<FreertosPoller>(context.domain());
#elif defined WIN_POLLER_ENABLED
        std::make_shared<WinPoller>(context.domain());
#endif
    return poller;
  });

  Factory<DnsResolver>([](AetherAppContext const& context) {
#if AE_SUPPORT_CLOUD_DNS
    auto dns_resolver =
#  if defined DNS_RESOLVE_ARES_ENABLED
        std::make_shared<DnsResolverCares>(*context.aether(), context.domain());
#  elif defined ESP32_DNS_RESOLVER_ENABLED
        std::make_shared<Esp32DnsResolver>(*context.aether(), context.domain());
#  endif
    return dns_resolver;
#else
    return std::make_shared<DnsResolver>(context.domain());
#endif
  });
}

RcPtr<AetherApp> AetherApp::Construct(AetherAppContext context) {
  auto app = MakeRcPtr<AetherApp>();
  auto* aether = context.aether();
  context.init_tele_(context);

  aether->adapter_registry = context.adapter_registry();

#if AE_SUPPORT_REGISTRATION
  aether->registration_cloud = context.registration_cloud();
#endif  // AE_SUPPORT_REGISTRATION
  aether->crypto = context.crypto();

  aether->poller = context.poller();

#if AE_SUPPORT_CLOUD_DNS
  aether->dns_resolver = context.dns_resolver();
#endif

  app->domain_facility_ = std::move(std::move(context).domain_storage_);
  app->domain_ = std::move(std::move(context).domain_);
  app->aether_ = std::move(std::move(context).aether_);
  return app;
}

AetherApp::~AetherApp() {
  // TODO: save internal states

  // reset telemetry before delete all objects
  TELE_SINK::Instance().SetTrap(nullptr);
  aether_.reset();
}

}  // namespace ae
