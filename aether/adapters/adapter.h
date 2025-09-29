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

#include "aether/obj/obj.h"
#include "aether/events/events.h"

#include "aether/access_points/access_point.h"

namespace ae {
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
  explicit Adapter(Domain* domain);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT()

  virtual std::vector<AccessPoint::ptr> access_points() = 0;

  virtual NewAccessPoint::Subscriber new_access_point();

 protected:
  NewAccessPoint new_access_point_event_;
};

}  // namespace ae

#endif  // AETHER_ADAPTERS_ADAPTER_H_ */
