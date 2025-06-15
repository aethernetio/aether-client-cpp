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

#include <map>
#include <optional>
#include <cassert>

#include "aether/config.h"
#include "aether/common.h"
#include "aether/ptr/ptr.h"
#include "aether/obj/domain.h"
#include "aether/global_ids.h"
#include "aether/type_traits.h"
#include "aether/reflect/reflect.h"

#include "aether/actions/action_trigger.h"
#include "aether/actions/action_processor.h"

#include "aether/aether.h"
#include "aether/crypto.h"
#include "aether/poller/poller.h"
#include "aether/adapters/adapter.h"
#include "aether/registration_cloud.h"
#include "aether/obj/component_context.h"

#include "aether/domain_storage/domain_storage_factory.h"

#include "aether/tele/tele.h"

namespace ae {
class AetherAppContext : public ComponentContext<AetherAppContext> {
  friend class AetherApp;
  class InitTele {
   public:
    InitTele();
  };

 public:
  explicit AetherAppContext()
      : AetherAppContext(DomainStorageFactory::Create) {}

  template <typename Func,
            AE_REQUIRERS((IsFunctor<Func, std::unique_ptr<IDomainStorage>()>))>
  explicit AetherAppContext(Func const& facility_factory)
      : init_tele_{},
        domain_storage_{facility_factory()},
        domain_{make_unique<Domain>(Now(), *domain_storage_)} {
    InitComponentContext();
#if defined AE_DISTILLATION
    // clean old state
    domain_storage_->CleanUp();

    aether_ = domain_->CreateObj<Aether>(kAether);
    assert(aether_);
#else
    aether_.SetId(kAether);
    domain_->LoadRoot(aether_);
    assert(aether_);
#endif  //  defined AE_DISTILLATION
  }

  Domain& domain() const { return *domain_; }
  Aether::ptr const& aether() const { return aether_; }

#if defined AE_DISTILLATION
  Adapter::ptr adapter() const { return Resolve<Adapter>(); }
  Cloud::ptr registration_cloud() const { return Resolve<Cloud>(); }
  Crypto::ptr crypto() const { return Resolve<Crypto>(); }
  IPoller::ptr poller() const { return Resolve<IPoller>(); }
  DnsResolver::ptr dns_resolver() const { return Resolve<DnsResolver>(); }

  template <typename TFunc>
  AetherAppContext&& AdapterFactory(TFunc&& func) && {
    Factory<Adapter>(std::forward<TFunc>(func));
    return std::move(*this);
  }

#  if AE_SUPPORT_REGISTRATION
  template <typename TFunc>
  AetherAppContext&& RegistrationCloudFactory(TFunc&& func) && {
    Factory<Cloud>(std::forward<TFunc>(func));
    return std::move(*this);
  }
#  endif  // AE_SUPPORT_REGISTRATION

  template <typename TFunc>
  AetherAppContext&& CryptoFactory(TFunc&& func) && {
    Factory<Crypto>(std::forward<TFunc>(func));
    return std::move(*this);
  }

  template <typename TFunc>
  AetherAppContext&& PollerFactory(TFunc&& func) && {
    Factory<IPoller>(std::forward<TFunc>(func));
    return std::move(*this);
  }

  template <typename TFunc>
  AetherAppContext&& DnsResolverFactory(TFunc&& func) && {
    Factory<DnsResolver>(std::forward<TFunc>(func));
    return std::move(*this);
  }
#endif  //  defined AE_DISTILLATION

 private:
  void InitComponentContext();

  InitTele init_tele_;
  std::unique_ptr<IDomainStorage> domain_storage_;
  std::unique_ptr<Domain> domain_;
  Aether::ptr aether_;
};

/**
 * \brief The enter point to the Aethernet application world
 */
class AetherApp {
 public:
  static Ptr<AetherApp> Construct(AetherAppContext&& context);

  AetherApp() = default;
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

  TimePoint Update(TimePoint current_time) {
    return domain_->Update(current_time);
  }
  void WaitUntil(TimePoint wakeup_time) {
    if (!IsExited()) {
      aether_->action_processor->get_trigger().WaitUntil(wakeup_time);
    }
  }

  Domain& domain() const { return *domain_; }
  Aether::ptr const& aether() const { return aether_; }

  // Action context protocol
  operator ActionContext() const { return ActionContext{*aether_}; }

  AE_REFLECT_MEMBERS(domain_facility_, domain_, aether_, exit_code_)

 private:
  std::unique_ptr<IDomainStorage> domain_facility_;
  std::unique_ptr<Domain> domain_;
  Aether::ptr aether_;

  std::optional<int> exit_code_;
};

}  // namespace ae

#endif  // AETHER_AETHER_APP_H_
