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

#ifndef AETHER_PORT_FILE_SYSTEMS_FACILITY_FACTORY_H_
#define AETHER_PORT_FILE_SYSTEMS_FACILITY_FACTORY_H_

#include "aether/ptr/ptr.h"
#include "aether/obj/domain.h"

namespace ae {
class DomainFacilityFactory {
 public:
  static Ptr<IDomainFacility> Create();
};
}  // namespace ae

#endif  // AETHER_PORT_FILE_SYSTEMS_FACILITY_FACTORY_H_
