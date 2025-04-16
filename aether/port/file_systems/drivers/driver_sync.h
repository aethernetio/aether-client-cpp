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

#ifndef AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SYNC_H_
#define AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SYNC_H_

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_set>

#include "aether/port/file_systems/drivers/driver_base.h"

namespace ae {

class DriverSync {
 public:
  DriverSync(enum DriverFsType fs_driver_type);
  ~DriverSync();
  void DriverRead(const std::string &path,
                  std::vector<std::uint8_t> &data_vector);
  void DriverWrite(const std::string &path,
                   const std::vector<std::uint8_t> &data_vector);
  void DriverDelete(const std::string &path);
  std::vector<std::string> DriverDir(const std::string &path);

 private:
  void DriverSyncronize_(std::unique_ptr<DriverBase> DrvSource,
                         std::unique_ptr<DriverBase> DrvDestination,
                         const std::string &path);
  enum DriverFsType fs_driver_type_ { DriverFsType::kDriverNone };
  std::unique_ptr<DriverBase> DriverSource{};
  std::unique_ptr<DriverBase> DriverDestination{};
};

}  // namespace ae

#endif  // AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SYNC_H_
