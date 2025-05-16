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

#ifndef AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_HEADER_H_
#define AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_HEADER_H_

#include <vector>
#include <cstdint>
#include <fstream>
#include <string>
#include <ios>
#include <system_error>

#include "aether/port/file_systems/drivers/driver_base.h"
#include "aether/packed_int.h"

#if defined FS_INIT
#  include FS_INIT
#else
#  error "FS_INIT should be defined"
#endif

#if defined FS_INIT_TEST
#  include FS_INIT_TEST
#else
#  error "FS_INIT_TEST should be defined"
#endif

namespace ae {

using HeaderSize = Packed<std::uint64_t, std::uint16_t, 8192>;

class DriverHeader {
  using Data = std::vector<std::uint8_t>;
  using VersionData = std::map<std::uint8_t, Data>;
  using ClassData = std::map<std::uint32_t, VersionData>;
  using ObjClassData = std::map<ae::ObjId, ClassData>;

 public:
  DriverHeader(DriverFsType fs_driver_type);
  ~DriverHeader();
  void DriverRead(const std::string &path,
                  std::vector<std::uint8_t> &data_vector, bool sync);
  void DriverWrite(const std::string &path,
                   const std::vector<std::uint8_t> &data_vector);
  void DriverDelete(const std::string &path);
  std::vector<PathStructure> DriverDir(const std::string &path);
  DriverFsType GetDriverFsType() { return fs_driver_type_; }

 private:
  DriverFsType fs_driver_type_{DriverFsType::kDriverNone};
  std::string ByteToHex(std::uint8_t ch);
  uint8_t HexToByte(const std::string &hex);
};

}  // namespace ae

#endif  // AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_HEADER_H_
