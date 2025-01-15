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

#ifndef AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_SPIFS_V2_H_
#define AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_SPIFS_V2_H_

#if (defined(ESP_PLATFORM))

#  include <map>
#  include <cstdint>

#  include "aether/obj/obj_id.h"
#  include "aether/port/file_systems/file_system_base.h"
#  include "aether/transport/low_level/tcp/data_packet_collector.h"
#  include "aether/transport/data_buffer.h"
#  include "aether/port/file_systems/drivers/driver_spifs.h"

namespace ae {
class FileSystemSpiFsV2Facility : public FileSystemBase {
  using Data = std::vector<std::uint8_t>;
  using VersionData = std::map<std::uint8_t, Data>;
  using ClassData = std::map<std::uint32_t, VersionData>;
  using ObjClassData = std::map<ae::ObjId, ClassData>;

 public:
  FileSystemSpiFsV2Facility();
  ~FileSystemSpiFsV2Facility();
  std::vector<uint32_t> Enumerate(const ae::ObjId& obj_id) override;
  void Store(const ae::ObjId& obj_id, std::uint32_t class_id,
             std::uint8_t version, const std::vector<uint8_t>& os) override;
  void Load(const ae::ObjId& obj_id, std::uint32_t class_id,
            std::uint8_t version, std::vector<uint8_t>& is) override;
  void Remove(const ae::ObjId& obj_id) override;
  void remove_all() override;

 private:
  DriverSpifs* driver_fs;
};
}  // namespace ae

#endif
#endif  // AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_SPIFS_V2_H_