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

#ifndef AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_BASE_H_
#define AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_BASE_H_

#include <cstdint>
#include <string>
#include <vector>
#include <map>

#include "aether/obj/domain.h"

#if defined FS_INIT
#  include FS_INIT
#else
#  error "FS_INIT should be defined"
#endif

namespace ae {

class DriverBase {
 public:
  virtual ~DriverBase() = default;
  virtual void DriverRead(const std::string &path,
                          std::vector<std::uint8_t> &data_vector,
                          bool sync) = 0;
  virtual void DriverWrite(const std::string &path,
                           const std::vector<std::uint8_t> &data_vector) = 0;
  virtual void DriverDelete(const std::string &path) = 0;
  virtual std::vector<std::string> DriverDir(const std::string &path) = 0;

 private:
};

}  // namespace ae

#endif  // AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_BASE_H_
