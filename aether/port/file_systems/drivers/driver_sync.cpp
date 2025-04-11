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

#include "aether/port/file_systems/drivers/driver_sync.h"
#include "aether/port/file_systems/drivers/driver_factory.h"

namespace ae {

DriverSync::DriverSync(enum DriverFsType fs_driver_type) {
  fs_driver_type_ = fs_driver_type;
  Driver = DriverFactory::Create(fs_driver_type_);
}

DriverSync::~DriverSync() {}

void DriverSync::DriverRead(const std::string &path, std::vector<std::uint8_t> &data_vector) {
  Driver->DriverRead(path, data_vector);
}

void DriverSync::DriverWrite(const std::string &path, const std::vector<std::uint8_t> &data_vector) {
  Driver->DriverWrite(path, data_vector);
}

void DriverSync::DriverDelete(const std::string &path) {
  Driver ->DriverDelete(path);
}

std::vector<std::string> DriverSync::DriverDir(const std::string &path) {
  std::vector<std::string> dirs_list{};

  dirs_list = Driver->DriverSync::DriverDir(path);

  return dirs_list;
}

}  // namespace ae
