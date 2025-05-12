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

#ifndef AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SPIFS_V2_H_
#define AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SPIFS_V2_H_

#if (defined(ESP_PLATFORM))

#  include <dirent.h>

#  include <string>
#  include <vector>
#  include <cstdint>

#  include "esp_spiffs.h"
#  include "spiffs_config.h"
#  include "sys/stat.h"
#  include "esp_err.h"

#  include "aether/port/file_systems/drivers/driver_base.h"
#  include "aether/port/file_systems/drivers/driver_factory.h"

namespace ae {

class DriverSpifsV2 : public DriverBase {
 public:
  DriverSpifsV2();
  ~DriverSpifsV2();
  void DriverRead(const std::string &path,
                  std::vector<std::uint8_t> &data_vector, bool sync) override;
  void DriverWrite(const std::string &path,
                   const std::vector<std::uint8_t> &data_vector) override;
  void DriverDelete(const std::string &path) override;
  std::vector<std::string> DriverDir(const std::string &path) override;
  void DriverFormat();

 private:
  esp_err_t DriverInit_();
  void DriverDeinit_();
  bool initialized_{false};

  static constexpr char kPartition[] = "storage";
  static constexpr char kBasePath[] = "/spiffs";
};

}  // namespace ae

#endif  // (defined(ESP_PLATFORM))

#endif  // AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SPIFS_V2_H_
