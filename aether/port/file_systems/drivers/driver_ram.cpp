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
#include "aether/port/file_systems/drivers/driver_functions.h"
#include "aether/port/file_systems/file_systems_tele.h"

namespace ae {

DriverRam::DriverRam(DriverFsType fs_driver_type)
    : DriverBase(fs_driver_type) {}

DriverRam::~DriverRam() {}

void DriverRam::DriverRead(const PathStructure &path,
                           std::vector<std::uint8_t> &data_vector, bool sync) {
  if (!sync) { 
    auto obj_it = state_ram_.find(path.obj_id);
    if (obj_it == state_ram_.end()) {
      return;
    }

    auto class_it = obj_it->second.find(path.class_id);
    if (class_it == obj_it->second.end()) {
      return;
    }

    auto version_it = class_it->second.find(path.version);
    if (version_it == class_it->second.end()) {
      return;
    }

    data_vector = version_it->second;
  }
}

void DriverRam::DriverWrite(const PathStructure &path,
                            const std::vector<std::uint8_t> &data_vector) {
  state_ram_[path.obj_id][path.class_id][path.version] =
      data_vector;
}

void DriverRam::DriverDelete(const PathStructure &path) {
  auto it = state_ram_.find(path.obj_id);
  if (it != state_ram_.end()) {
    AE_TELE_DEBUG(FsObjRemoved, "Removed object {}",
                  path.obj_id.ToString());
    state_ram_.erase(it);
  } else {
    AE_TELE_ERROR(FsRemoveObjIdNoFound, "Object id={} not found!",
                  path.obj_id.ToString());
  }
}

std::vector<PathStructure> DriverRam::DriverDir(const PathStructure &path) {
  PathStructure res_path{};
  std::vector<PathStructure> dirs_list{};

  // Path is "state/version/obj_id/class_id"
  AE_TELE_DEBUG(FsDir, "Path {}", GetPathString(path));
  for (auto &ItemObjClassData : state_ram_) {
    for (auto &ItemClassData : ItemObjClassData.second) {
      for (auto &ItemVersionData : ItemClassData.second) {
        // Path is "state/version/obj_id/class_id"
        res_path.version = ItemVersionData.first;
        res_path.obj_id = ItemObjClassData.first;
        res_path.class_id = ItemClassData.first;
        dirs_list.push_back(res_path);
      }
    }
  }

  return dirs_list;
}

}  // namespace ae
