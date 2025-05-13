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

#ifndef AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_STD_H_
#define AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_STD_H_

#include <cstdint>
#include <string>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/port/file_systems/drivers/driver_base.h"

#if defined AE_FILE_SYSTEM_STD_ENABLED

namespace ae {

class DriverStd : public DriverBase {
 public:
  DriverStd(DriverFsType fs_driver_type);
  ~DriverStd();
  void DriverRead(const std::string &path,
                  std::vector<std::uint8_t> &data_vector, bool sync) override;
  void DriverWrite(const std::string &path,
                   const std::vector<std::uint8_t> &data_vector) override;
  void DriverDelete(const std::string &path) override;
  std::vector<std::string> DriverDir(const std::string &path) override;

 private:
};

}  // namespace ae

#endif  // defined AE_FILE_SYSTEM_STD_ENABLED
#endif  // AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_STD_H_
