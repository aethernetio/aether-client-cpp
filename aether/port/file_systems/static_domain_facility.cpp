/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/port/file_systems/static_domain_facility.h"

#if defined STATIC_DOMAIN_FACILITY_ENABLED

#  include <iterator>
#  include <algorithm>

#  include "aether/port/file_systems/static_object_types.h"

#  include "aether/port/file_systems/file_systems_tele.h"

#  include FS_INIT

namespace ae {
StaticDomainFacility::StaticDomainFacility() = default;
StaticDomainFacility::~StaticDomainFacility() = default;

void StaticDomainFacility::Store(ObjId const& /* obj_id */,
                                 std::uint32_t /* class_id */,
                                 std::uint8_t /* version */,
                                 std::vector<uint8_t> const& /* os */) {}

std::vector<std::uint32_t> StaticDomainFacility::Enumerate(
    ObjId const& obj_id) {
  std::vector<std::uint32_t> res;
  // object_map is defined in FS_INIT
  auto const classes = object_map.find(obj_id.id());
  if (classes == std::end(object_map)) {
    AE_TELED_DEBUG("Object id {} not found", obj_id.id());
    return res;
  }
  AE_TELED_DEBUG("Enumerate obj id {} classes count {}", obj_id.id(),
                 classes->second.size());
  res.reserve(classes->second.size());
  std::copy(std::begin(classes->second), std::end(classes->second),
            std::back_inserter(res));
  return res;
}

void StaticDomainFacility::Load(ObjId const& obj_id, std::uint32_t class_id,
                                std::uint8_t version,
                                std::vector<uint8_t>& is) {
  // state_map is defined in FS_INIT
  auto obj_path = ObjectPathKey{obj_id.id(), class_id, version};
  auto const data = state_map.find(obj_path);
  if (data == std::end(state_map)) {
    AE_TELED_DEBUG("Object id {} class id {} version {} not found", obj_id.id(),
                   class_id, static_cast<int>(version));
    return;
  }
  AE_TELED_DEBUG("Load obj id {} class id {} version {} data size {}",
                 obj_id.id(), class_id, static_cast<int>(version),
                 data->second.size());
  is.reserve(is.size() + data->second.size());
  std::copy(std::begin(data->second), std::end(data->second),
            std::back_inserter(is));
}

void StaticDomainFacility::Remove(ObjId const& /* obj_id */) {}

#  if defined AE_DISTILLATION
void StaticDomainFacility::CleanUp() {}
#  endif
}  // namespace ae

#endif
