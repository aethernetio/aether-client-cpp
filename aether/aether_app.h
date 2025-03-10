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
#include "aether/actions/action_registry.h"
#include "aether/actions/action_processor.h"

#include "aether/aether.h"
#include "aether/crypto/key.h"
#include "aether/crypto/sign.h"
#include "aether/adapters/adapter.h"
#include "aether/registration_cloud.h"

#include "aether/port/file_systems/facility_factory.h"

#include "aether/tele/tele.h"

namespace ae {
class AetherAppConstructor {
  friend class AetherApp;
  class InitTele {
   public:
    InitTele();
  };

 public:
  explicit AetherAppConstructor()
      : AetherAppConstructor(DomainFacilityFactory::Create) {}

  template <typename Func,
            AE_REQUIRERS((IsFunctor<Func, std::unique_ptr<IDomainFacility>()>))>
  explicit AetherAppConstructor(Func const& facility_factory)
      : init_tele_{},
        domain_facility_{facility_factory()},
        domain_{make_unique<Domain>(Now(), *domain_facility_)} {
#if defined AE_DISTILLATION
    aether_ = domain_->CreateObj<Aether>(kAether);
    assert(aether_);
#else
    aether_.SetId(kAether);
    domain_->LoadRoot(aether_);
    assert(aether_);
#endif  //  defined AE_DISTILLATION
  }

#if defined AE_DISTILLATION
  template <
      typename Func,
      AE_REQUIRERS((IsFunctor<Func, Adapter::ptr(Domain* domain,
                                                 Aether::ptr const& aether)>))>
  AetherAppConstructor&& Adapter(Func const& adapter_factory) {
    adapter_ = adapter_factory(domain_.get(), aether_);
    return std::move(*this);
  }

#  if AE_SUPPORT_REGISTRATION
  template <typename Func,
            AE_REQUIRERS((IsFunctor<Func, RegistrationCloud::ptr(
                                              Domain* domain,
                                              Aether::ptr const& aether)>))>
  AetherAppConstructor&& RegCloud(Func const& registration_cloud_factory) {
    registration_cloud_ = registration_cloud_factory(domain_.get(), aether_);
    return std::move(*this);
  }
#  endif

  template <typename Func,
            AE_REQUIRERS((IsFunctor<Func, std::pair<SignatureMethod, Key>()>))>
  AetherAppConstructor&& SignedKey(Func const& get_signed_key) {
    auto signed_key = get_signed_key();
    signed_keys_.insert(signed_key);
    return std::move(*this);
  }

#endif

 private:
  InitTele init_tele_;
  std::unique_ptr<IDomainFacility> domain_facility_;
  std::unique_ptr<Domain> domain_;
  Aether::ptr aether_;
#if defined AE_DISTILLATION
#  if AE_SUPPORT_REGISTRATION
  RegistrationCloud::ptr registration_cloud_;
#  endif
  Adapter::ptr adapter_;
  std::map<SignatureMethod, Key> signed_keys_;
#endif
};

/**
 * \brief The enter point to the Aethernet application world
 */
class AetherApp {
 public:
  static Ptr<AetherApp> Construct(AetherAppConstructor&& constructor);

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
  ActionRegistry& get_registry() {
    return aether_->action_processor->get_registry();
  }
  ActionTrigger& get_trigger() {
    return aether_->action_processor->get_trigger();
  }

  AE_REFLECT_MEMBERS(domain_facility_, domain_, aether_, exit_code_)

 private:
  std::unique_ptr<IDomainFacility> domain_facility_;
  std::unique_ptr<Domain> domain_;
  Aether::ptr aether_;

  std::optional<int> exit_code_;
};

}  // namespace ae

#endif  // AETHER_AETHER_APP_H_
