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

#include "aether/port/file_systems/file_system_spifs_v1.h"

#if AE_FILE_SYSTEM_SPIFS_V1_ENABLED == 1

#  include "aether/tele/tele.h"

namespace ae {

FileSystemSpiFsV1Facility::FileSystemSpiFsV1Facility() {
  driver_sync_fs_ = std::make_unique<DriverSync>(DriverFsType::kDriverNone,
                                                 DriverFsType::kDriverSpifsV1);
  AE_TELED_DEBUG("New FileSystemSpiFsV1Facility instance created!");
}

FileSystemSpiFsV1Facility::~FileSystemSpiFsV1Facility() {
  AE_TELED_DEBUG("FileSystemSpiFsV1Facility instance deleted!");
}

std::vector<uint32_t> FileSystemSpiFsV1Facility::Enumerate(
    const ae::ObjId& obj_id) {
  std::vector<std::uint32_t> classes;
  std::vector<std::string> dirs_list{};
  std::string path{"state"};
  std::string file{};

  dirs_list = driver_sync_fs_->DriverDir(path);

  for (auto dir : dirs_list) {
    AE_TELE_DEBUG(FsEnumerated, "Dir {}", dir);
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

void FileSystemSpiFsV1Facility::Store(const ae::ObjId& obj_id,
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

void FileSystemSpiFsV1Facility::Load(const ae::ObjId& obj_id,
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

void FileSystemSpiFsV1Facility::Remove(const ae::ObjId& obj_id) {
  std::string path{"state"};

  auto version_dirs = driver_sync_fs_->DriverDir(path);

  for (auto const& ver_dir : version_dirs) {
    auto obj_dirs = driver_sync_fs_->DriverDir(ver_dir);
    auto obj_it = std::find_if(
        std::begin(obj_dirs), std::end(obj_dirs), [&](auto const& path) {
          return path.find("/" + obj_id.ToString() + "/") != std::string::npos;
        });
    if (obj_it != std::end(obj_dirs)) {
      driver_sync_fs_->DriverDelete(*obj_it);
      AE_TELE_DEBUG(FsObjRemoved, "Removed object {} of version dir {}",
                    obj_id.ToString(), ver_dir);
    }
  }
}

#  if defined AE_DISTILLATION
void FileSystemSpiFsV1Facility::CleanUp() {
  std::string path{"dump"};

  driver_sync_fs_->DriverDelete(path);
}
#  endif

}  // namespace ae

#endif  // AE_FILE_SYSTEM_SPIFS_V1_ENABLED
