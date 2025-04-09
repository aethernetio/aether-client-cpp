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

#include <ios>
#include <set>
#include <string>
#include <fstream>
#include <filesystem>
#include <system_error>

#include "aether/port/file_systems/drivers/driver_std.h"
#include "aether/port/file_systems/drivers/driver_functions.h"
#include "aether/port/file_systems/file_systems_tele.h"

namespace ae {

DriverStd::DriverStd(){}

DriverStd::~DriverStd(){}

void DriverStd::DriverStdRead(const std::string &path, std::vector<std::uint8_t> &data_vector){
  ae::PathStructure path_struct{};

  path_struct = GetPathStructure(path);
  
  auto obj_dir = std::filesystem::path("state") / std::to_string(path_struct.version) /
                 path_struct.obj_id.ToString();
  auto p = obj_dir / std::to_string(path_struct.class_id);
  std::ifstream f(p.c_str(), std::ios::in | std::ios::binary);
  if (!f.good()) {
    AE_TELE_ERROR(FsLoadObjClassIdNotFound, "Unable to open file {}",
                  p.string());
    return;
  }

  auto ec = std::error_code{};
  auto file_size = std::filesystem::file_size(p, ec);
  if (ec) {
    AE_TELED_ERROR("Unable to get file size {}", ec.message());
    return;
  }

  data_vector.resize(static_cast<std::size_t>(file_size));
  f.read(reinterpret_cast<char*>(data_vector.data()),
         static_cast<std::streamsize>(data_vector.size()));
}

void DriverStd::DriverStdWrite(const std::string &path, const std::vector<std::uint8_t> &data_vector){
  ae::PathStructure path_struct{};

  path_struct = GetPathStructure(path);

  auto obj_dir = std::filesystem::path("state") / std::to_string(path_struct.version) /
                 path_struct.obj_id.ToString();
  std::filesystem::create_directories(obj_dir);
  auto p = obj_dir / std::to_string(path_struct.class_id);
  std::ofstream f(p.c_str(),
                  std::ios::out | std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<const char*>(data_vector.data()),
          static_cast<std::streamsize>(data_vector.size()));
}

void DriverStd::DriverStdDelete(const std::string &path){
  std::filesystem::remove_all(path);
}

std::vector<std::string> DriverStd::DriverStdDir(const std::string &path){
  
}

}  // namespace ae
