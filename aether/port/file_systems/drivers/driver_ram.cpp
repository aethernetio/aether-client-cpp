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

#include "aether/port/file_systems/drivers/driver_ram.h"

namespace ae {
  
DriverRam::DriverRam() {}

DriverRam::~DriverRam() {}

void DriverRam::DriverRamRead(const std::string &path,
                              std::vector<std::uint8_t> &data_vector) {}

void DriverRam::DriverRamWrite(const std::string &path,
                               const std::vector<std::uint8_t> &data_vector) {}

void DriverRam::DriverRamDelete(const std::string &path) {}

std::vector<std::string> DriverRam::DriverRamDir(const std::string &path) {
  std::vector<std::string> dirs_list{};
  
  return dirs_list;
}

}  // namespace ae
