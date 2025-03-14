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

#ifndef AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_STD_H_
#define AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_STD_H_

#include <vector>

#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
     defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))

#  define AE_FILE_SYSTEM_STD_ENABLED 1

#  include "aether/obj/domain.h"

namespace ae {
class FileSystemStdFacility : public IDomainFacility {
 public:
  FileSystemStdFacility();
  ~FileSystemStdFacility() override;

  std::vector<uint32_t> Enumerate(const ae::ObjId& obj_id) override;
  void Store(const ae::ObjId& obj_id, std::uint32_t class_id,
             std::uint8_t version, const std::vector<uint8_t>& os) override;
  void Load(const ae::ObjId& obj_id, std::uint32_t class_id,
            std::uint8_t version, std::vector<uint8_t>& is) override;
  void Remove(const ae::ObjId& obj_id) override;
#  if defined AE_DISTILLATION
  void CleanUp() override;
#  endif
};
}  // namespace ae

#endif
#endif  // AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_STD_H_ */
