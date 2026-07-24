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

#include <cstdlib>
#include <iostream>

// IWYU pragma: begin_keeps
#include "aether/types/address_parser.h"
#include "aether/types/literal_array.h"

#include "aether/adapters/ethernet.h"
#include "aether/adapters/wifi_adapter.h"
#include "aether/crypto.h"
#include "aether/crypto/key.h"
#include "aether/global_ids.h"
#include "aether/poller/epoll_poller.h"
#include "aether/poller/freertos_poller.h"
#include "aether/poller/kqueue_poller.h"
#include "aether/poller/win_poller.h"
#include "aether/registration_cloud.h"

#include "aether/dns/dns_c_ares.h"
#include "aether/dns/esp32_dns_resolve.h"

#include "aether/tele.h"
#include "aether/tele_compile_options.h"
// IWYU pragma: end_keeps

namespace ae {

void AetherAppContext::TelemetryInit() {
#if AE_TELE_ENABLED
  // Init telemetry sink
  if (!TELE_SINK::Instance().trap()) {
#  if AE_TELE_LOG_CONSOLE && AE_TELE_LOG_TO_STATISTICS
    // telemetry to both console and statistics
    auto trap = std::make_shared<
        ae::tele::ProxyTrap<ae::tele::IoStreamTrap,
                            ae::tele::StatisticsTrap<AE_STATISTICS_MAX_SIZE>>>(
        std::make_shared<ae::tele::IoStreamTrap>(std::cout),
        std::make_shared<ae::tele::StatisticsTrap<AE_STATISTICS_MAX_SIZE>>());
#  elif AE_TELE_LOG_CONSOLE
    // telemetry to console only
    auto trap = std::make_shared<ae::tele::IoStreamTrap>(std::cout);
#  elif AE_TELE_LOG_TO_STATISTICS
    // telemetry to statistics only
    auto trap =
        std::make_shared<ae::tele::StatisticsTrap<AE_STATISTICS_MAX_SIZE>>();
#  else
    // NONE trap is enabled
    return;
#  endif
    TELE_SINK::Instance().SetTrap(trap);
  }
#  if AE_TELE_LOG_TO_STATISTICS
  else {
    // TODO: is it possible to fix?
    // IF AE_TELE_LOG_TO_STATISTICS is defined user are not allowed to set
    // custom trap, because it's static_casts in TeleStatistics object
    std::cerr
        << "Custom trap are not allowed with AE_TELE_LOG_TO_STATISTICS==1\n";
    std::abort();
  }
#  endif

  AE_TELE_ENV(CompileOptions());
  AE_TELE_INFO(AetherStarted);
  Registry::GetRegistry().Log();
#endif
}

static Aether::ptr AetherFactory(AetherAppContext const& context) {
  /**
   * If it's production or filtration mode, declare and load Aether.
   * However on filtration mode, if Aether fails to load, create a new
   * instance.
   */
#if !AE_DISTILLATION || AE_FILTRATION
  auto a = Aether::ptr::Declare(
      CreateWith{context.domain()}.with_id(GlobalId::kAether));
  a.Load();
  if (a.is_loaded()) {
    return a;
  }
#endif
#if AE_DISTILLATION || AE_FILTRATION
  // Create a new instance of Aether if it's not loaded
  return Aether::ptr::Create(
      CreateWith{context.domain()}.with_id(GlobalId::kAether));
#else  // pure production mode
  assert(false && "Failed to load Aether");
  return Aether::ptr{};
#endif
}

static TeleStatistics::ptr TeleStatisticsFactory(
    AetherAppContext const& context) {
  auto tele_statistics = TeleStatistics::ptr{context.aether()->tele_statistics};
  if (tele_statistics.is_valid()) {
    return tele_statistics;
  }
#if AE_DISTILLATION || AE_FILTRATION
  return TeleStatistics::ptr::Create(
      CreateWith{context.domain()}.with_id(GlobalId::kTeleStatistics));
#else
  assert(false && "Failed to load TeleStatistics");
  return tele::TeleStatistics::ptr{};
#endif
}

#if AE_DISTILLATION
static AdapterRegistry::ptr AdapterRegistryFactory(
    AetherAppContext const& context) {
  auto ap = AdapterRegistry::ptr{context.aether()->adapter_registry};
  if (ap.is_valid()) {
    return ap;
  }
  // Create a new instance of AdapterRegistry if it's not loaded
  ap = AdapterRegistry::ptr::Create(
      CreateWith{context.domain()}
          .with_id(GlobalId::kAdapterRegistry)
          .with_flags(ObjFlags::kUnloadedByDefault));
  // add adapters
  auto adapters = context.adapters();
  assert(!adapters.empty() && "Adapter factories must not be empty");
  for (auto const& a : context.adapters()) {
    ap->Add(a);
  }
  return ap;
}

static Adapter::ptr DefaultAdapterFactory(AetherAppContext const& context) {
  return EthernetAdapter::ptr::Create(
      CreateWith{context.domain()}
          .with_id(GlobalId::kEthernetAdapter)
          .with_flags(ObjFlags::kUnloadedByDefault),
      context.aether(), context.poller(), context.dns_resolver());
}

#  if AE_SUPPORT_REGISTRATION
static Cloud::ptr RegistrationCloudFactory(AetherAppContext const& context) {
  auto cloud = Cloud::ptr{context.aether()->registration_cloud};
  if (cloud.is_valid()) {
    return cloud;
  }
  auto reg_c = RegistrationCloud::ptr::Create(
      CreateWith{context.domain()}
          .with_id(GlobalId::kRegistrationCloud)
          .with_flags(ObjFlags::kUnloadedByDefault),
      context.aether());
#    if defined _AE_REG_CLOUD_IP
  reg_c->AddServerSettings(
      Endpoint{{AddressParser::StringToAddress(_AE_REG_CLOUD_IP), 9010},
               Protocol::kTcp});
#    endif
  reg_c->AddServerSettings(Endpoint{
      {AddressParser::StringToAddress("registration.aethernet.io"), 9010},
      Protocol::kTcp});
  // hardcoded ip address in case named address is not supported
  reg_c->AddServerSettings(Endpoint{
      {AddressParser::StringToAddress("34.60.244.148"), 9010}, Protocol::kTcp});
  return reg_c;
}
#  endif  // AE_SUPPORT_REGISTRATION

static Crypto::ptr CryptoFactory(AetherAppContext const& context) {
  auto crypto = Crypto::ptr{context.aether()->crypto};
  if (crypto.is_valid()) {
    return crypto;
  }
  crypto = Crypto::ptr::Create(CreateWith{context.domain()}
                                   .with_id(GlobalId::kCrypto)
                                   .with_flags(ObjFlags::kUnloadedByDefault));
#  if AE_SIGNATURE == AE_ED25519
  crypto->signs_pk_[ae::SignatureMethod::kEd25519] = ae::SodiumSignPublicKey{
      MakeArray("4F202A94AB729FE9B381613AE77A8A7D89EDAB9299C33"
                "20D1A0B994BA710CCEB")};
#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
  crypto->signs_pk_[ae::SignatureMethod::kHydroSignature] =
      ae::HydrogenSignPublicKey{
          MakeArray("883B4D7E0FB04A38CA12B3A451B00942048858263EE6E"
                    "6D61150F2EF15F40343")};
#  endif  // AE_SIGNATURE == AE_ED25519
  return crypto;
}

static IPoller::ptr PollerFactory(AetherAppContext const& context) {
  auto poller = IPoller::ptr{context.aether()->poller};
  if (poller.is_valid()) {
    return poller;
  }

#  if defined EPOLL_POLLER_ENABLED
  return EpollPoller::ptr::Create(
      CreateWith{context.domain()}
          .with_id(GlobalId::kPoller)
          .with_flags(ObjFlags::kUnloadedByDefault));
#  elif defined KQUEUE_POLLER_ENABLED
  return KqueuePoller::ptr::Create(
      CreateWith{context.domain()}
          .with_id(GlobalId::kPoller)
          .with_flags(ObjFlags::kUnloadedByDefault));
#  elif defined FREERTOS_POLLER_ENABLED
  return FreertosPoller::ptr::Create(
      CreateWith{context.domain()}
          .with_id(GlobalId::kPoller)
          .with_flags(ObjFlags::kUnloadedByDefault));
#  elif defined WIN_POLLER_ENABLED
  return WinPoller::ptr::Create(CreateWith{context.domain()}
                                    .with_id(GlobalId::kPoller)
                                    .with_flags(ObjFlags::kUnloadedByDefault));
#  else
  return {};
#  endif
}

static DnsResolver::ptr DnsResolverFactory(AetherAppContext const& context) {
  auto dns_resolver = DnsResolver::ptr{context.aether()->dns_resolver};
  if (dns_resolver.is_valid()) {
    return dns_resolver;
  }
#  if AE_SUPPORT_CLOUD_DNS
#    if defined DNS_RESOLVE_ARES_ENABLED
  return DnsResolverCares::ptr::Create(
      CreateWith{context.domain()}
          .with_id(GlobalId::kDnsResolver)
          .with_flags(ObjFlags::kUnloadedByDefault),
      context.aether());
#    elif defined ESP32_DNS_RESOLVER_ENABLED
  return Esp32DnsResolver::ptr::Create(
      CreateWith{context.domain()}
          .with_id(GlobalId::kDnsResolver)
          .with_flags(ObjFlags::kUnloadedByDefault),
      context.aether());
#    else
  return {};
#    endif
#  else
  return DnsResolver::ptr::Create(
      CreateWith{context.domain()}
          .with_id(GlobalId::kDnsResolver)
          .with_flags(ObjFlags::kUnloadedByDefault));
#  endif
}

static Client::ptr ClientPrefabFactory(AetherAppContext const& context) {
  auto client_prefab = context.aether()->client_prefab;
  if (client_prefab.is_valid()) {
    return client_prefab;
  }
  return Client::ptr::Create(CreateWith{context.domain()}
                                 .with_id(GlobalId::kClientFactory)
                                 .with_flags(ObjFlags::kUnloadedByDefault),
                             context.aether());
}
#endif  //  AE_DISTILLATION

void AetherAppContext::InitComponentContext() {
  domain_.Factory([](AetherAppContext const& context) {
    auto& domain_storage = *context.domain_storage_.Resolve();
#if AE_DISTILLATION && !AE_FILTRATION
    // clean old state
    domain_storage.CleanUp();
#endif  // AE_DISTILLATION

    return std::make_unique<Domain>(Now(), domain_storage);
  });

  if (!aether_) {
    aether_.Factory(::ae::AetherFactory);
  }
  if (!tele_statistics_) {
    tele_statistics_.Factory(::ae::TeleStatisticsFactory);
  }

#if AE_DISTILLATION
  if (!adapter_registry_) {
    adapter_registry_.Factory(::ae::AdapterRegistryFactory);
  }

  if (adapters_.empty()) {
    // add one default adapter
    auto& a = adapters_.emplace_back();
    a.Factory(::ae::DefaultAdapterFactory);
  }

#  if AE_SUPPORT_REGISTRATION
  if (!reg_cloud_) {
    reg_cloud_.Factory(::ae::RegistrationCloudFactory);
  }
#  endif  // AE_SUPPORT_REGISTRATION

  if (!crypto_) {
    crypto_.Factory(::ae::CryptoFactory);
  }

  if (!poller_) {
    poller_.Factory(::ae::PollerFactory);
  }

  if (!dns_resolver_) {
    dns_resolver_.Factory(::ae::DnsResolverFactory);
  }

  if (!client_prefab_) {
    client_prefab_.Factory(::ae::ClientPrefabFactory);
  }
#endif  //  AE_DISTILLATION
}

RcPtr<AetherApp> AetherApp::Construct(AetherAppContext context) {
  // init all the components in context
  context.InitComponentContext();

  auto app = MakeRcPtr<AetherApp>();
  app->aether_ = context.aether();
#if AE_DISTILLATION
  app->aether_->tele_statistics = context.tele_statistics_.Resolve(context);
  app->aether_->client_prefab = context.client_prefab_.Resolve(context);

  app->aether_->adapter_registry = context.adapter_registry();

#  if AE_SUPPORT_REGISTRATION
  app->aether_->registration_cloud = context.reg_cloud();
#  endif  // AE_SUPPORT_REGISTRATION

  app->aether_->crypto = context.crypto();
  app->aether_->poller = context.poller();

#  if AE_SUPPORT_CLOUD_DNS
  app->aether_->dns_resolver = context.dns_resolver();
#  endif

  app->aether_.Save();
#endif  // AE_DISTILLATION

  // save domain from context to the app
  app->domain_facility_ =
      std::move(std::move(context).domain_storage_.Resolve());
  app->domain_ = std::move(std::move(context).domain_.Resolve(context));
  return app;
}

AetherApp::~AetherApp() {
  // save aether_ state on exit
  if (aether_) {
    aether_.Save();
  }

  // reset telemetry before delete all objects
  TELE_SINK::Instance().SetTrap(nullptr);
  aether_.Reset();
}

}  // namespace ae
