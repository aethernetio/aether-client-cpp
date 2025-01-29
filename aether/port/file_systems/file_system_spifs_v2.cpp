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

#  include "aether/tele/tele.h"

namespace ae {

FileSystemSpiFsV2Facility::FileSystemSpiFsV2Facility() {
  driver_fs = new DriverSpifs();
  AE_TELED_DEBUG("New FileSystemSpiFsV2 instance created!");
  // driver_fs->DriverSpifsFormat();
}

FileSystemSpiFsV2Facility::~FileSystemSpiFsV2Facility() {
  delete driver_fs;
  AE_TELED_DEBUG("FileSystemSpiFsV2 instance deleted!");
}

std::vector<uint32_t> FileSystemSpiFsV2Facility::Enumerate(
    const ae::ObjId& obj_id) {
  std::vector<std::uint32_t> classes;
  std::vector<std::string> dirs_list{};
  std::string path{"state"};
  std::string file{};

  dirs_list = driver_fs->DriverSpifsDir(path);

  for (auto dir : dirs_list) {
    auto pos1 = dir.find("/" + obj_id.ToString() + "/");
    if (pos1 != std::string::npos) {
      AE_TELED_DEBUG("Object id={} found!", obj_id.ToString());
      auto pos2 = dir.rfind("/");
      if (pos2 != std::string::npos) {
        file.assign(dir, pos2 + 1, dir.size() - pos2 - 1);
        auto enum_class = static_cast<uint32_t>(std::stoul(file));
        AE_TELED_DEBUG("Add to the classes {}", enum_class);
        classes.push_back(enum_class);
      }
    }
  }

  return classes;
}

void FileSystemSpiFsV2Facility::Store(const ae::ObjId& obj_id,
                                      std::uint32_t class_id,
                                      std::uint8_t version,
                                      const std::vector<uint8_t>& os) {
  std::string path{};

  path = "state/" + std::to_string(version) + "/" + obj_id.ToString() + "/" +
         std::to_string(class_id);

  driver_fs->DriverSpifsWrite(path, os);

  AE_TELED_DEBUG("Saved {} size: {}", path, os.size());
  AE_TELED_DEBUG("Object id={} & class id = {} saved!", obj_id.ToString(),
                 class_id);
}

void FileSystemSpiFsV2Facility::Load(const ae::ObjId& obj_id,
                                     std::uint32_t class_id,
                                     std::uint8_t version,
                                     std::vector<uint8_t>& is) {
  std::string path{};

  path = "state/" + std::to_string(version) + "/" + obj_id.ToString() + "/" +
         std::to_string(class_id);

  driver_fs->DriverSpifsRead(path, is);

  AE_TELED_DEBUG("Loaded {}!", path);
}

void FileSystemSpiFsV2Facility::Remove(const ae::ObjId& obj_id) {
  std::vector<std::string> dirs_list{};
  std::string path{"state"};

  dirs_list = driver_fs->DriverSpifsDir(path);

  for (auto dir : dirs_list) {
    auto pos1 = dir.find("/" + obj_id.ToString() + "/");
    if (pos1 != std::string::npos) {
      driver_fs->DriverSpifsDelete(dir);
      AE_TELED_DEBUG("Removed {}!", obj_id.ToString());
    }
  }
}

void FileSystemSpiFsV2Facility::remove_all() {
  std::string path{"state"};

  driver_fs->DriverSpifsDelete(path);
}

}  // namespace ae

#endif  // AE_FILE_SYSTEM_SPIFS_V2_ENABLED
