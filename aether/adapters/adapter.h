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

#ifndef AETHER_ADAPTERS_ADAPTER_H_
#define AETHER_ADAPTERS_ADAPTER_H_

#include <vector>

#include "aether-objects/obj/obj.h"
#include "aether/actions/action.h"
#include "aether/events/event_subscription.h"
#include "aether/events/events.h"

#include "aether/access_points/access_point.h"

namespace ae {
/// Runtime shutdown operation owned by an adapter, never serialized.
class IAdapterStop : public Action {};

/// Completes immediately or follows a driver-owned shutdown action.
class AdapterStop final : public IAdapterStop {
 public:
  AdapterStop() { Finish(); }
  explicit AdapterStop(Action& action) {
    if (action.is_finished()) {
      Finish();
      return;
    }
    finished_sub_ = action.finished_event().Subscribe([this]() {
      finished_sub_.Reset();
      Finish();
    });
  }

 private:
  Subscription finished_sub_;
};
/**
 * \brief The interface to control network adapter.
 * It must configure interface and provide list of access points.
 */
class Adapter : public Obj {
  AE_OBJECT(Adapter, Obj, 0)

 protected:
  Adapter() = default;

 public:
  using NewAccessPoint = Event<void(AccessPoint::ptr const&)>;

#ifdef AE_DISTILLATION
  explicit Adapter(ObjProp prop);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT()

  virtual std::vector<AccessPoint::ptr> access_points() = 0;

  // Stop runtime network resources. The adapter owns the returned action and
  // keeps it alive until destruction, including after completion.
  virtual IAdapterStop& Stop() = 0;

  virtual NewAccessPoint::Subscriber new_access_point();

 protected:
  NewAccessPoint new_access_point_event_;
};

}  // namespace ae

#endif  // AETHER_ADAPTERS_ADAPTER_H_ */
