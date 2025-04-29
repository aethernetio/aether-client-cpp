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

#ifndef AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SPIFS_V1_H_
#define AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SPIFS_V1_H_

#if (defined(ESP_PLATFORM))

#  include <string>
#  include <vector>
#  include <cstdint>

#  include "esp_spiffs.h"
#  include "spiffs_config.h"
#  include "sys/stat.h"
#  include "esp_err.h"

#  include "aether/obj/domain.h"
#  include "aether/port/file_systems/drivers/driver_base.h"
#  include "aether/transport/data_buffer.h"
#  include "aether/transport/low_level/tcp/data_packet_collector.h"

namespace ae {
  using Data = std::vector<std::uint8_t>;
  using VersionData = std::map<std::uint8_t, Data>;
  using ClassData = std::map<std::uint32_t, VersionData>;
  using ObjClassData = std::map<ae::ObjId, ClassData>;
  
  static ObjClassData state_spifs_;

class DriverSpifsV1: public DriverBase{
 public:
  DriverSpifsV1();
  ~DriverSpifsV1();
  void DriverRead(const std::string &path,
                       std::vector<std::uint8_t> &data_vector, bool sync) override;
  void DriverWrite(const std::string &path,
                        const std::vector<std::uint8_t> &data_vector)  override;
  void DriverDelete(const std::string &path)  override;
  std::vector<std::string> DriverDir(const std::string &path)  override;
  void DriverFormat();

 private:
  esp_err_t DriverInit_();
  void DriverDeinit_();
  bool initialized_{false};
  void LoadObjData_(ObjClassData& obj_data);
  void SaveObjData_(ObjClassData& obj_data);
  void DriverRead_(const std::string &path,
                       std::vector<std::uint8_t> &data_vector);
  void DriverWrite_(const std::string &path,
                        const std::vector<std::uint8_t> &data_vector);
  
  static constexpr char kPartition[] = "storage";
  static constexpr char kBasePath[] = "/spiffs";
};

}  // namespace ae

#endif  // (defined(ESP_PLATFORM))

#endif  // AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SPIFS_V1_H_
