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

#ifndef AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_SPIFS_V1_H_
#define AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_SPIFS_V1_H_

#include "aether/config.h"

#if defined ESP_PLATFORM && AE_SUPPORT_SPIFS_V1_FS
#  define AE_FILE_SYSTEM_SPIFS_V1_ENABLED 1

#  include <map>
#  include <cstdint>

#  include "aether/obj/obj_id.h"
#  include "aether/obj/domain.h"
#  include "aether/transport/data_buffer.h"
#  include "aether/transport/low_level/tcp/data_packet_collector.h"
#  include "aether/port/file_systems/drivers/driver_spifs.h"

namespace ae {
class FileSystemSpiFsV1Facility : public FileSystemBase {
  using Data = std::vector<std::uint8_t>;
  using VersionData = std::map<std::uint8_t, Data>;
  using ClassData = std::map<std::uint32_t, VersionData>;
  using ObjClassData = std::map<ae::ObjId, ClassData>;

 public:
  FileSystemSpiFsV1Facility();
  ~FileSystemSpiFsV1Facility() override;
  std::vector<uint32_t> Enumerate(const ae::ObjId& obj_id) override;
  void Store(const ae::ObjId& obj_id, std::uint32_t class_id,
             std::uint8_t version, const std::vector<uint8_t>& os) override;
  void Load(const ae::ObjId& obj_id, std::uint32_t class_id,
            std::uint8_t version, std::vector<uint8_t>& is) override;
  void Remove(const ae::ObjId& obj_id) override;

#  if defined AE_DISTILLATION
  void CleanUp() override;
#  endif
 private:
  void _LoadObjData(ObjClassData& obj_data);
  void _SaveObjData(ObjClassData& obj_data);

  DriverSpifs* driver_fs;
};
}  // namespace ae

#endif
#endif  // AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_SPIFS_V1_H_
