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

#ifndef AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SPIFS_H_
#define AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SPIFS_H_

#if (defined(ESP_PLATFORM))

#  include <dirent.h>

#  include <string>
#  include <vector>
#  include <cstdint>

#  include "esp_spiffs.h"
#  include "spiffs_config.h"
#  include "sys/stat.h"
#  include "esp_err.h"

namespace ae {

class DriverSpifs {
 public:
  DriverSpifs();
  ~DriverSpifs();
  void DriverSpifsRead(const std::string &path,
                       std::vector<std::uint8_t> &data_vector);
  void DriverSpifsWrite(const std::string &path,
                        const std::vector<std::uint8_t> &data_vector);
  void DriverSpifsDelete(const std::string &path);
  std::vector<std::string> DriverSpifsDir(const std::string &path);
  void DriverSpifsFormat();

 private:
  esp_err_t _DriverSpifsInit();
  void _DriverSpifsDeinit();

  static constexpr char PARTITION[] = "storage";
  static constexpr char BASE_PATH[] = "/spiffs";
  bool _initialized{false};
};

}  // namespace ae

#endif  // (defined(ESP_PLATFORM))

#endif  // AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SPIFS_H_
