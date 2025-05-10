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

#if defined AE_FILE_SYSTEM_STD_ENABLED
namespace ae {

DriverStd::DriverStd() {}

DriverStd::~DriverStd() {}

void DriverStd::DriverRead(const std::string &path,
                           std::vector<std::uint8_t> &data_vector, bool sync) {
  if (sync == false) {
    ae::PathStructure path_struct{};

    path_struct = GetPathStructure(path);

    auto obj_dir = std::filesystem::path("state") /
                   std::to_string(path_struct.version) /
                   path_struct.obj_id.ToString();
    auto p = obj_dir / std::to_string(path_struct.class_id);
    std::ifstream f(p.c_str(), std::ios::in | std::ios::binary);
    if (!f.good()) {
      // Replacing backslashes with straight ones
      std::string obj_path = p.string();
#  if (defined(_WIN64) || defined(_WIN32))
      // Replacing backslashes with straight ones
      std::replace(obj_path.begin(), obj_path.end(), '\\', '/');
#  endif
      AE_TELE_ERROR(FsLoadObjClassIdNotFound, "Unable to open file {}",
                    obj_path);
      return;
    }

    auto ec = std::error_code{};
    auto file_size = std::filesystem::file_size(p, ec);
    if (ec) {
      AE_TELED_ERROR("Unable to get file size {}", ec.message());
      return;
    }

    data_vector.resize(static_cast<std::size_t>(file_size));
    f.read(reinterpret_cast<char *>(data_vector.data()),
           static_cast<std::streamsize>(data_vector.size()));
  }
}

void DriverStd::DriverWrite(const std::string &path,
                            const std::vector<std::uint8_t> &data_vector) {
  ae::PathStructure path_struct{};

  path_struct = GetPathStructure(path);

  auto obj_dir = std::filesystem::path("state") /
                 std::to_string(path_struct.version) /
                 path_struct.obj_id.ToString();
  std::filesystem::create_directories(obj_dir);
  auto p = obj_dir / std::to_string(path_struct.class_id);
  std::ofstream f(p.c_str(),
                  std::ios::out | std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<const char *>(data_vector.data()),
          static_cast<std::streamsize>(data_vector.size()));
}

void DriverStd::DriverDelete(const std::string &path) {
  std::string obj_path = path;
#  if (defined(_WIN64) || defined(_WIN32))
  // Replacing straight ones with backslashes
  std::replace(obj_path.begin(), obj_path.end(), '/', '\\');
#  endif
  std::filesystem::remove_all(obj_path);
}

std::vector<std::string> DriverStd::DriverDir(const std::string &path) {
  std::vector<std::string> dirs_list{};

  auto state_dir = std::filesystem::path{path};
  auto ec = std::error_code{};
  for (auto const &version_dir :
       std::filesystem::directory_iterator(state_dir, ec)) {
    auto ec2 = std::error_code{};
    for (auto const &obj_dir :
         std::filesystem::directory_iterator(version_dir, ec2)) {
      auto ec3 = std::error_code{};
      for (auto const &f : std::filesystem::directory_iterator(obj_dir, ec3)) {
        std::string name = f.path().string();
#  if (defined(_WIN64) || defined(_WIN32))
        // Replacing backslashes with straight ones
        std::replace(name.begin(), name.end(), '\\', '/');
#  endif
        dirs_list.push_back(name);
      }
      if (ec3) {
        AE_TELED_ERROR("Unable to open directory with error {}", ec3.message());
      }
    }
    if (ec2) {
      AE_TELED_ERROR("Unable to open directory with error {}", ec2.message());
    }
  }

  if (ec) {
    AE_TELED_ERROR("Unable to open directory with error {}", ec.message());
  }

  return dirs_list;
}

}  // namespace ae

#endif  // defined AE_FILE_SYSTEM_STD_ENABLED
