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

#ifndef AETHER_AETHER_APP_H_
#define AETHER_AETHER_APP_H_

#include <array>
#include <cassert>
#include <optional>

#include "aether/config.h"
#include "aether/common.h"
#include "aether/ptr/ptr.h"
#include "aether/ptr/rc_ptr.h"
#include "aether/obj/domain.h"
#include "aether/type_traits.h"
#include "aether/types/small_function.h"

#include "aether/events/events.h"   // IWYU pragma: keep
#include "aether/actions/action.h"  // IWYU pragma: keep
#include "aether/actions/action_ptr.h"
#include "aether/actions/action_trigger.h"
#include "aether/actions/action_processor.h"

#include "aether/cloud.h"
#include "aether/aether.h"
#include "aether/crypto.h"
#include "aether/uap/uap.h"
#include "aether/poller/poller.h"
#include "aether/dns/dns_resolve.h"
#include "aether/adapter_registry.h"
#include "aether/obj/component_factory.h"

#include "aether/domain_storage/domain_storage_factory.h"

namespace ae {
class AetherAppContext {
  friend class AetherApp;
  class TelemetryInit {
   public:
    TelemetryInit();
    void operator()(AetherAppContext const& context) const;
  };

 public:
  explicit AetherAppContext()
      : AetherAppContext(DomainStorageFactory::Create, TelemetryInit{}) {}

  template <typename Func, typename TeleInit = TelemetryInit,
            AE_REQUIRERS((IsFunctor<Func, std::unique_ptr<IDomainStorage>()>)),
            AE_REQUIRERS((IsFunctor<TeleInit, void(AetherAppContext const&)>))>
  explicit AetherAppContext(Func&& domain_storage_factory,
                            TeleInit const& tele_init = TelemetryInit{})
      : init_tele_{tele_init} {
    domain_storage_.Factory(std::forward<Func>(domain_storage_factory));
  }

  AE_CLASS_MOVE_ONLY(AetherAppContext)

  Domain& domain() const { return *domain_.Resolve(*this); }
  Aether::ptr& aether() const { return aether_.Resolve(*this); }

  AdapterRegistry::ptr& adapter_registry() const {
    return adapter_registry_.Resolve(*this);
  }
  std::vector<Adapter::ptr> adapters() const {
    std::vector<Adapter::ptr> res;
    res.reserve(adapters_.size());
    for (auto const& a : adapters_) {
      res.emplace_back(a.Resolve(*this));
    }
    return res;
  }
  Cloud::ptr& reg_cloud() const { return reg_cloud_.Resolve(*this); }
  Crypto::ptr& crypto() const { return crypto_.Resolve(*this); }
  IPoller::ptr& poller() const { return poller_.Resolve(*this); }
  DnsResolver::ptr& dns_resolver() const {
    return dns_resolver_.Resolve(*this);
  }
  Uap::ptr& uap() const { return uap_.Resolve(*this); }

#if AE_DISTILLATION
  template <typename TFunc>
  AetherAppContext&& AdaptersFactory(TFunc&& func) && {
    adapter_registry_.Factory(std::forward<TFunc>(func));
    return std::move(*this);
  }

  template <typename TFunc>
  AetherAppContext&& AddAdapterFactory(TFunc&& func) && {
    auto& back = adapters_.emplace_back();
    back.Factory(std::forward<TFunc>(func));
    return std::move(*this);
  }

#  if AE_SUPPORT_REGISTRATION
  template <typename TFunc>
  AetherAppContext&& RegistrationCloudFactory(TFunc&& func) && {
    reg_cloud_.Factory(std::forward<TFunc>(func));
    return std::move(*this);
  }
#  endif  // AE_SUPPORT_REGISTRATION

  template <typename TFunc>
  AetherAppContext&& CryptoFactory(TFunc&& func) && {
    crypto_.Factory(std::forward<TFunc>(func));
    return std::move(*this);
  }

  template <typename TFunc>
  AetherAppContext&& PollerFactory(TFunc&& func) && {
    poller_.Factory(std::forward<TFunc>(func));
    return std::move(*this);
  }

#  if AE_SUPPORT_CLOUD_DNS
  template <typename TFunc>
  AetherAppContext&& DnsResolverFactory(TFunc&& func) && {
    dns_resolver_.Factory(std::forward<TFunc>(func));
    return std::move(*this);
  }
#  endif
  template <typename TFunc>
  AetherAppContext&& UapFactory(TFunc&& func) && {
    uap_.Factory(std::forward<TFunc>(func));
    return std::move(*this);
  }
#endif  // AE_DISTILLATION

 private:
  void InitComponentContext();

  ComponentFactory<std::unique_ptr<IDomainStorage>> domain_storage_;
  ComponentFactory<AetherAppContext, std::unique_ptr<Domain>> domain_;
  ComponentFactory<AetherAppContext, Aether::ptr> aether_;
  ComponentFactory<AetherAppContext, AdapterRegistry::ptr> adapter_registry_;
  std::vector<ComponentFactory<AetherAppContext, Adapter::ptr>> adapters_;
  ComponentFactory<AetherAppContext, Cloud::ptr> reg_cloud_;
  ComponentFactory<AetherAppContext, Crypto::ptr> crypto_;
  ComponentFactory<AetherAppContext, IPoller::ptr> poller_;
  ComponentFactory<AetherAppContext, DnsResolver::ptr> dns_resolver_;
  ComponentFactory<AetherAppContext, Uap::ptr> uap_;
  ComponentFactory<AetherAppContext, Client::ptr> client_prefab_;
  ComponentFactory<AetherAppContext, tele::TeleStatistics::ptr>
      tele_statistics_;

  SmallFunction<void(AetherAppContext const&)> init_tele_;
};

/**
 * \brief The enter point to the Aethernet application world
 */
class AetherApp {
  friend auto MakeRcPtr<AetherApp>() noexcept;

 public:
  static RcPtr<AetherApp> Construct(AetherAppContext context);

  ~AetherApp();

  /**
   * \brief Mark application as exited
   */
  void Exit(int code = 0) {
    exit_code_ = code;
    aether_->action_processor->get_trigger().Trigger();
  }

  bool IsExited() const { return exit_code_.has_value(); }
  int ExitCode() const {
    assert(exit_code_.has_value());
    return *exit_code_;
  }

  /**
   * \brief Run one iteration of application update loop.
   */
  TimePoint Update(TimePoint current_time) {
    return domain_->Update(current_time);
  }

  /**
   * \brief Wait untile timeout or application event triggered.
   */
  void WaitUntil(TimePoint wakeup_time) {
    if (!IsExited()) {
      aether_->action_processor->get_trigger().WaitUntil(wakeup_time);
    }
  }

  /**
   * \brief Wait until all actions are excited.
   */
  template <typename... TAction>
  void WaitActions(ActionPtr<TAction>&... actions) {
    WaitEvents(actions->StatusEvent()...);
  }

  /**
   * \brief Wait until event is emitted.
   */
  template <typename... TEvents>
  void WaitEvents(TEvents&&... event) {
    std::size_t done_count = 0;
    std::array subs{Subscription{std::forward<TEvents>(event).Subscribe(
        [&done_count](auto&&...) { done_count++; })}...};
    while (!IsExited()) {
      auto new_time = Update(Now());
      if (done_count == sizeof...(TEvents)) {
        return;
      }
      WaitUntil(new_time);
    }
  }

  Domain& domain() const { return *domain_; }
  Aether::ptr const& aether() const { return aether_; }

  // Action context protocol
  operator ActionContext() const { return ActionContext{*aether_}; }

 private:
  AetherApp() = default;

  std::unique_ptr<IDomainStorage> domain_facility_;
  std::unique_ptr<Domain> domain_;
  Aether::ptr aether_;

  std::optional<int> exit_code_;
};

}  // namespace ae

#endif  // AETHER_AETHER_APP_H_
