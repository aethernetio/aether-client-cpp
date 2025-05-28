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

#ifndef AETHER_PORT_FILE_SYSTEMS_STATIC_DOMAIN_FACILITY_H_
#define AETHER_PORT_FILE_SYSTEMS_STATIC_DOMAIN_FACILITY_H_

#if defined FS_INIT
#  define STATIC_DOMAIN_FACILITY_ENABLED 1

#  include "aether/obj/domain.h"

namespace ae {
class StaticDomainFacility final : public IDomainFacility {
 public:
  StaticDomainFacility();
  ~StaticDomainFacility() override;

  void Store(ObjId const& obj_id, std::uint32_t class_id, std::uint8_t version,
             std::vector<uint8_t> const& os) override;
  std::vector<std::uint32_t> Enumerate(ObjId const& obj_id) override;
  void Load(ObjId const& obj_id, std::uint32_t class_id, std::uint8_t version,
            std::vector<uint8_t>& is) override;
  void Remove(ObjId const& obj_id) override;

#  if defined AE_DISTILLATION
  /**
   * \brief Clean up the whole saved state
   */
  void CleanUp() override;
#  endif
};
}  // namespace ae

#endif
#endif  // AETHER_PORT_FILE_SYSTEMS_STATIC_DOMAIN_FACILITY_H_
