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
#include <concepts>
#include <memory>
#include <optional>
#include <type_traits>

#include "aether-objects/env/env.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/ptr/ptr.h"

#include "aether/common.h"
#include "aether/config.h"
#include "aether/memory.h"

#include "aether/actions/action.h"  // IWYU pragma: keep
#include "aether/events/events.h"   // IWYU pragma: keep

#include "aether/adapter_registry.h"
#include "aether/aether.h"
#include "aether/client.h"
#include "aether/cloud.h"
#include "aether/crypto.h"
#include "aether/dns/dns_resolve.h"
#include "aether/env.h"
#include "aether/obj/component_factory.h"
#include "aether/poller/poller.h"
#include "aether/tele_statistics.h"

#include "aether/domain_storage/domain_storage_factory.h"

namespace ae {

/**
 * \brief Provided Env must meet specified requirenments
 */
template <typename T>
concept AeEnvConcept = requires(T& t) {
  requires(std::derived_from<T, Env>);
  { t.scheduler() } -> std::same_as<TaskScheduler&>;
  { t.event_system() } -> std::same_as<EventSystem&>;
  { t.SetAether(std::declval<ObjPtr<Aether> const&>()) };
};

/**
 * \brief AetherApp construction context.
 * Provide here all factories required for different objects.
 * Factories could call each other via context.<object>() this atomaticaly
 * creates dependency graph for them. Avoid cycle dependencies.
 * To set factory use AetherAppContext.<Object>Factory(...)&& method. Notice it
 * must be called on rvalue context and returns rvalue, so it's easier to chain
 * all set factory together. Setup factories available only in DESTILLATION
 * mode.
 * To use factory use AetherAppContext.<objcet>() const method.
 */
class AetherAppContext {
  friend class AetherApp;

 public:
  /**
   * \brief Create aether with default factories for domain storage and env
   * For domain storage DomainStorageFactory::Create is used \see
   * domain_storage/domain_storage_factory.h
   * For Env AeEnv is created \see env.h
   */
  explicit AetherAppContext()
      : AetherAppContext(DomainStorageFactory::Create,
                         []() { return std::make_unique<AeEnv>(); }) {}

  template <typename DomainFactory,
            // must create derived from domain storage type
            std::derived_from<IDomainStorage> ds =
                typename std::invoke_result_t<DomainFactory>::element_type>
  explicit AetherAppContext(DomainFactory&& domain_storage_factory)
      : AetherAppContext(std::forward<DomainFactory>(domain_storage_factory),
                         []() { return std::make_unique<AeEnv>(); }) {}

  /**
   * \brief Provide the most basic factories for aether app.
   * Factory for domain storage and env
   */
  template <typename DomainFactory, typename EnvFactory,
            // must create derived from domain storage type
            std::derived_from<IDomainStorage> ds =
                typename std::invoke_result_t<DomainFactory>::element_type,
            // must create ae env concept type
            AeEnvConcept env =
                typename std::invoke_result_t<EnvFactory>::element_type>
  explicit AetherAppContext(DomainFactory&& domain_storage_factory,
                            EnvFactory&& env_factory)
      :  // Save set aether function
        set_aether_to_env_{[](Env* e, Aether::ptr const& a) {
          static_cast<env*>(e)->SetAether(a);
        }} {
    TelemetryInit();
    domain_storage_.Factory(
        std::forward<DomainFactory>(domain_storage_factory));
    env_.Factory(std::forward<EnvFactory>(env_factory));
  }

  AE_CLASS_MOVE_ONLY(AetherAppContext)

  /// Access to domain objcet
  Domain& domain() const { return *domain_.Resolve(*this); }
  /// The main root Aether object
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
#endif  // AE_DISTILLATION

 private:
  void TelemetryInit();
  void TeleStatisticsInit(TeleStatistics::ptr const& tele_statistics) const;

  void InitComponentContext();

  ComponentFactory<std::unique_ptr<IDomainStorage>> domain_storage_;
  ComponentFactory<std::unique_ptr<Env>> env_;
  ComponentFactory<AetherAppContext, std::unique_ptr<Domain>> domain_;
  ComponentFactory<AetherAppContext, Aether::ptr> aether_;
  ComponentFactory<AetherAppContext, AdapterRegistry::ptr> adapter_registry_;
  std::vector<ComponentFactory<AetherAppContext, Adapter::ptr>> adapters_;
  ComponentFactory<AetherAppContext, Cloud::ptr> reg_cloud_;
  ComponentFactory<AetherAppContext, Crypto::ptr> crypto_;
  ComponentFactory<AetherAppContext, IPoller::ptr> poller_;
  ComponentFactory<AetherAppContext, DnsResolver::ptr> dns_resolver_;
  ComponentFactory<AetherAppContext, Client::ptr> client_prefab_;
  ComponentFactory<AetherAppContext, TeleStatistics::ptr> tele_statistics_;

  bool tele_statistics_trap_is_set{false};
  void (*set_aether_to_env_)(Env* env, Aether::ptr const&);
};

/**
 * \brief The enter point to the Aethernet application world
 */
class AetherApp {
 public:
  static std::unique_ptr<AetherApp> Construct(AetherAppContext context);

  ~AetherApp();

  /**
   * \brief Mark application as exited
   */
  void Exit(int code = 0) {
    exit_code_ = code;
    // wake up the task thread
    try_get_env<TaskScheduler>(*this)->Task([]() {});
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
    return try_get_env<TaskScheduler>(*this)->Update(current_time);
  }

  /**
   * \brief Wait untile timeout or application event triggered.
   */
  void WaitUntil(TimePoint wakeup_time) {
    if (!IsExited()) {
      try_get_env<TaskScheduler>(*this)->WaitUntil(wakeup_time);
    }
  }

  /**
   * \brief Wait until all actions are excited.
   */
  template <typename... TAction>
    requires(std::is_base_of_v<Action, TAction> && ...)
  void WaitActions(TAction&... actions) {
    WaitEvents(actions.finished_event()...);
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
  Env* get_env() const { return env_.get(); }
  Aether::ptr const& aether() const { return aether_; }

 private:
  AetherApp() = default;

  std::unique_ptr<IDomainStorage> domain_facility_;
  std::unique_ptr<Env> env_;
  std::unique_ptr<Domain> domain_;
  Aether::ptr aether_;

  std::optional<int> exit_code_;
};

}  // namespace ae

#endif  // AETHER_AETHER_APP_H_
