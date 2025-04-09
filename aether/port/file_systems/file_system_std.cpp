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

#include "aether/port/file_systems/file_system_std.h"

#if defined AE_FILE_SYSTEM_STD_ENABLED

#  include <ios>
#  include <set>
#  include <string>
#  include <fstream>
#  include <filesystem>
#  include <system_error>

#  include "aether/port/file_systems/file_systems_tele.h"

namespace ae {

FileSystemStdFacility::FileSystemStdFacility() {
  AE_TELED_DEBUG("New FileSystemStdFacility instance created!");
}

FileSystemStdFacility::~FileSystemStdFacility() {
  AE_TELED_DEBUG("FileSystemStdFacility instance deleted!");
}

std::vector<uint32_t> FileSystemStdFacility::Enumerate(
    const ae::ObjId& obj_id) {
  // collect unique classes
  std::set<uint32_t> classes;

  auto state_dir = std::filesystem::path{"state"};
  auto ec = std::error_code{};
  for (auto const& version_dir :
       std::filesystem::directory_iterator(state_dir, ec)) {
    auto obj_dir = version_dir.path() / obj_id.ToString();

    auto ec2 = std::error_code{};
    for (auto const& f : std::filesystem::directory_iterator(obj_dir, ec2)) {
      auto enum_class =
          static_cast<uint32_t>(std::stoul(f.path().filename().string()));
      classes.insert(enum_class);
    }
    AE_TELE_DEBUG(FsEnumerated, "Enumerated classes {}", classes);
    if (ec2) {
      AE_TELED_ERROR("Unable to open directory with error {}", ec2.message());
    }
  }

  if (ec) {
    AE_TELE_ERROR(FsEnumObjIdNotFound, "Unable to open directory with error {}",
                  ec.message());
  }

  return std::vector<std::uint32_t>(classes.begin(), classes.end());
}

void FileSystemStdFacility::Store(const ae::ObjId& obj_id,
                                  std::uint32_t class_id, std::uint8_t version,
                                  const std::vector<uint8_t>& os) {

  std::string path{};

  path = "state/" + std::to_string(version) + "/" + obj_id.ToString() + "/" +
         std::to_string(class_id);

  driver_std_fs.DriverStdWrite(path, os);

  AE_TELE_DEBUG(
      FsObjSaved, "Saved object id={}, class id={}, version={}, size={}",
      obj_id.ToString(), class_id, static_cast<int>(version), os.size());
}

void FileSystemStdFacility::Load(const ae::ObjId& obj_id,
                                 std::uint32_t class_id, std::uint8_t version,
                                 std::vector<uint8_t>& is) {
  std::string path{};

  path = "state/" + std::to_string(version) + "/" + obj_id.ToString() + "/" +
         std::to_string(class_id);

  driver_std_fs.DriverStdRead(path, is);

  AE_TELE_DEBUG(
      FsObjLoaded, "Loaded object id={}, class id={}, version={}, size={}",
      obj_id.ToString(), class_id, static_cast<int>(version), is.size());
}

void FileSystemStdFacility::Remove(const ae::ObjId& obj_id) {
  std::filesystem::path state_dir = std::filesystem::path{"state"};

  auto ec = std::error_code{};
  for (auto const& version_dir :
       std::filesystem::directory_iterator(state_dir, ec)) {
    auto obj_dir = version_dir.path() / obj_id.ToString();
    driver_std_fs.DriverStdDelete(obj_dir.string());
    AE_TELE_DEBUG(FsObjRemoved, "Removed object {} of version {}",
                  obj_id.ToString(), version_dir.path().filename());
  }
  if (ec) {
    AE_TELE_ERROR(FsRemoveObjIdNoFound,
                  "Unable to open directory with error {}", ec.message());
  }
}

#  if defined AE_DISTILLATION
void FileSystemStdFacility::CleanUp() {
  std::string path{"state"};

  driver_std_fs.DriverStdDelete(path);

  AE_TELED_DEBUG("All objects have been removed!");
}
#  endif

}  // namespace ae

#endif  // AE_FILE_SYSTEM_STD_ENABLED
