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

#include "aether/port/file_systems/file_system_spifs_v2.h"

#if defined AE_FILE_SYSTEM_SPIFS_V2_ENABLED

#  include "aether/port/file_systems/file_systems_tele.h"

namespace ae {

FileSystemSpiFsV2Facility::FileSystemSpiFsV2Facility() {
  driver_sync_fs_ = std::make_unique<DriverSync>(DriverFsType::kDriverNone,
                                                 DriverFsType::kDriverSpifsV2);
  AE_TELED_DEBUG("New FileSystemSpiFsV2Facility instance created!");
}

FileSystemSpiFsV2Facility::~FileSystemSpiFsV2Facility() {
  AE_TELED_DEBUG("FileSystemSpiFsV2Facility instance deleted!");
}

std::vector<uint32_t> FileSystemSpiFsV2Facility::Enumerate(
    const ae::ObjId& obj_id) {
  std::vector<std::uint32_t> classes;
  std::vector<std::string> dirs_list{};
  std::string path{"state"};
  std::string file{};

  dirs_list = driver_sync_fs_->DriverDir(path);

  for (auto dir : dirs_list) {
    auto pos1 = dir.find("/" + obj_id.ToString() + "/");
    if (pos1 != std::string::npos) {
      auto pos2 = dir.rfind("/");
      if (pos2 != std::string::npos) {
        file.assign(dir, pos2 + 1, dir.size() - pos2 - 1);
        auto enum_class = static_cast<uint32_t>(std::stoul(file));
        classes.push_back(enum_class);
      }
    }
  }
  AE_TELE_DEBUG(FsEnumerated, "Enumerated classes {}", classes);

  return classes;
}

void FileSystemSpiFsV2Facility::Store(const ae::ObjId& obj_id,
                                      std::uint32_t class_id,
                                      std::uint8_t version,
                                      const std::vector<uint8_t>& os) {
  std::string path{};

  path = "state/" + std::to_string(version) + "/" + obj_id.ToString() + "/" +
         std::to_string(class_id);

  driver_sync_fs_->DriverWrite(path, os);

  AE_TELE_DEBUG(
      FsObjSaved, "Saved object id={}, class id={}, version={}, size={}",
      obj_id.ToString(), class_id, static_cast<int>(version), os.size());
}

void FileSystemSpiFsV2Facility::Load(const ae::ObjId& obj_id,
                                     std::uint32_t class_id,
                                     std::uint8_t version,
                                     std::vector<uint8_t>& is) {
  std::string path{};

  path = "state/" + std::to_string(version) + "/" + obj_id.ToString() + "/" +
         std::to_string(class_id);

  driver_sync_fs_->DriverRead(path, is);

  AE_TELE_DEBUG(
      FsObjLoaded, "Loaded object id={}, class id={}, version={}, size={}",
      obj_id.ToString(), class_id, static_cast<int>(version), is.size());
}

void FileSystemSpiFsV2Facility::Remove(const ae::ObjId& obj_id) {
  std::string path{"state"};
  ae::PathStructure path_struct{};
  
  // Path is "state/version/obj_id/class_id"
  auto version_dirs = driver_sync_fs_->DriverDir(path);
  
  for (auto const& ver_dir : version_dirs) {   
    path_struct = GetPathStructure(path + "/" + ver_dir);

    if (obj_id == path_struct.obj_id) {
      driver_sync_fs_->DriverDelete(path + "/" + ver_dir);
    }
  }
}

#  if defined AE_DISTILLATION
void FileSystemSpiFsV2Facility::CleanUp() {
  std::string path{"state"};

  driver_sync_fs_->DriverDelete(path);

  AE_TELED_DEBUG("All objects have been removed!");
}
#  endif

}  // namespace ae

#endif  // AE_FILE_SYSTEM_SPIFS_V2_ENABLED
