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

#ifndef AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_HEADER_H_
#define AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_HEADER_H_

#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <ios>
#include <system_error>

#if defined FS_INIT
#  include FS_INIT
#endif

namespace ae {

class DriverHeader {
 public:
  DriverHeader();
  ~DriverHeader();
  void DriverHeaderRead(const std::string &path,
                        std::vector<std::uint8_t> &data_vector);
  void DriverHeaderWrite(const std::string &path,
                         const std::vector<std::uint8_t> &data_vector);
  void DriverHeaderDelete(const std::string &path);

 private:
  std::string ByteToHex(std::uint8_t ch);
  uint8_t HexToByte(const std::string &hex);
};

}  // namespace ae

#endif  // AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_HEADER_H_
