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

#ifndef AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_RAM_H_
#define AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_RAM_H_

#define AE_FILE_SYSTEM_RAM_ENABLED 1

#include <map>
#include <vector>
#include <cstdint>

#include "aether/port/file_systems/file_system_base.h"
#include "aether/port/file_systems/drivers/driver_header.h"

namespace ae {
class FileSystemRamFacility : public FileSystemBase {
  using Data = std::vector<std::uint8_t>;
  using VersionData = std::map<std::uint8_t, Data>;
  using ClassData = std::map<std::uint32_t, VersionData>;
  using ObjClassData = std::map<ae::ObjId, ClassData>;

 public:
  FileSystemRamFacility();
  ~FileSystemRamFacility() override;

  std::vector<uint32_t> Enumerate(const ae::ObjId& obj_id) override;
  void Store(const ae::ObjId& obj_id, std::uint32_t class_id,
             std::uint8_t version, const std::vector<uint8_t>& os) override;
  void Load(const ae::ObjId& obj_id, std::uint32_t class_id,
            std::uint8_t version, std::vector<uint8_t>& is) override;
  void Remove(const ae::ObjId& obj_id) override;
  void remove_all() override;
  void out_header();

 private:
  ObjClassData state_;
  DriverHeader *driver_fs;
};
}  // namespace ae
#endif  // AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_RAM_H_ */
