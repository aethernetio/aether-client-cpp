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

#if AE_FILE_SYSTEM_STD_ENABLED == 1

#  include <filesystem>

#  include "aether/port/file_systems/file_systems_tele.h"
#  include "aether/port/file_systems/drivers/driver_functions.h"
#  include "aether/port/file_systems/drivers/driver_sync.h"
#  include "aether/port/file_systems/drivers/driver_header.h"
#  include "aether/port/file_systems/drivers/driver_std.h"
#  include "aether/port/file_systems/drivers/driver_ram.h"
#  include "aether/port/file_systems/drivers/driver_spifs_v1.h"
#  include "aether/port/file_systems/drivers/driver_spifs_v2.h"


namespace ae {

FileSystemStdFacility::FileSystemStdFacility() {
  std::unique_ptr<DriverHeader> driver_source{
      std::make_unique<DriverHeader>(DriverFsType::kDriverNone)};
  std::unique_ptr<DriverBase> driver_destination{
      std::make_unique<DriverStd>(DriverFsType::kDriverStd)};
  driver_sync_fs_ = std::make_unique<DriverSync>(std::move(driver_source),
                                                 std::move(driver_destination));
  AE_TELED_DEBUG("New FileSystemStdFacility instance created!");
}

FileSystemStdFacility::~FileSystemStdFacility() {
  AE_TELED_DEBUG("FileSystemStdFacility instance deleted!");
}

std::vector<uint32_t> FileSystemStdFacility::Enumerate(
    const ae::ObjId& obj_id) {
  std::vector<std::uint32_t> classes;
  std::vector<PathStructure> dirs_list{};
  PathStructure path{};

  path.obj_id = obj_id;

  dirs_list = driver_sync_fs_->DriverDir(path);

  for (auto const& dir : dirs_list) {
    if (obj_id == dir.obj_id) {
      classes.push_back(dir.class_id);
    }
  }
  AE_TELE_DEBUG(FsEnumerated, "Enumerated classes {}", classes);

  return classes;
}

void FileSystemStdFacility::Store(const ae::ObjId& obj_id,
                                  std::uint32_t class_id, std::uint8_t version,
                                  const std::vector<uint8_t>& os) {
  PathStructure path{};

  path.version = version;
  path.obj_id = obj_id;
  path.class_id = class_id;

  driver_sync_fs_->DriverWrite(path, os);

  AE_TELE_DEBUG(
      FsObjSaved, "Saved object id={}, class id={}, version={}, size={}",
      obj_id.ToString(), class_id, static_cast<int>(version), os.size());
}

void FileSystemStdFacility::Load(const ae::ObjId& obj_id,
                                 std::uint32_t class_id, std::uint8_t version,
                                 std::vector<uint8_t>& is) {
  PathStructure path{};

  path.version = version;
  path.obj_id = obj_id;
  path.class_id = class_id;

  driver_sync_fs_->DriverRead(path, is);

  AE_TELE_DEBUG(
      FsObjLoaded, "Loaded object id={}, class id={}, version={}, size={}",
      obj_id.ToString(), class_id, static_cast<int>(version), is.size());
}

void FileSystemStdFacility::Remove(const ae::ObjId& obj_id) {
  PathStructure path{};

  // Path is "state/version/obj_id/class_id"
  path.obj_id = obj_id;

  auto dirs = driver_sync_fs_->DriverDir(path);

  for (auto const& dir : dirs) {
    if (obj_id == dir.obj_id) {
      driver_sync_fs_->DriverDelete(dir);
      AE_TELE_DEBUG(FsObjRemoved, "Removed object {} of directory {}",
                    obj_id.ToString(), GetPathString(dir, 3, true));
    }
  }
}

#  if defined AE_DISTILLATION
void FileSystemStdFacility::CleanUp() {
  PathStructure path{};

  auto dirs = driver_sync_fs_->DriverDir(path);

  for (auto const& dir : dirs) {
    driver_sync_fs_->DriverDelete(dir);
  }

  AE_TELED_DEBUG("All objects have been removed!");
}
#  endif

}  // namespace ae

#endif  // AE_FILE_SYSTEM_STD_ENABLED
