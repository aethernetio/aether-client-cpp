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

DriverRam::DriverRam() {}

DriverRam::~DriverRam() {}

void DriverRam::DriverRead(const std::string &path,
                              std::vector<std::uint8_t> &data_vector, bool sync) {
  if (sync == false) {
    ae::PathStructure path_struct{};

    path_struct = GetPathStructure(path);

    auto obj_it = state_ram_.find(path_struct.obj_id);
    if (obj_it == state_ram_.end()) {
      return;
    }

    auto class_it = obj_it->second.find(path_struct.class_id);
    if (class_it == obj_it->second.end()) {
      return;
    }

    auto version_it = class_it->second.find(path_struct.version);
    if (version_it == class_it->second.end()) {
      return;
    }

    data_vector = version_it->second;
  }
}

void DriverRam::DriverWrite(const std::string &path,
                               const std::vector<std::uint8_t> &data_vector) {
  ae::PathStructure path_struct{};

  path_struct = GetPathStructure(path);

  state_ram_[path_struct.obj_id][path_struct.class_id][path_struct.version] =
      data_vector;
}

void DriverRam::DriverDelete(const std::string &path) {
  ae::PathStructure path_struct{};

  path_struct = GetPathStructure(path);

  auto it = state_ram_.find(path_struct.obj_id);
  if (it != state_ram_.end()) {
    AE_TELE_DEBUG(FsObjRemoved, "Removed object {}",
                  path_struct.obj_id.ToString());
    state_ram_.erase(it);
  } else {
    AE_TELE_ERROR(FsRemoveObjIdNoFound, "Object id={} not found!",
                  path_struct.obj_id.ToString());
  }
}

std::vector<std::string> DriverRam::DriverDir(const std::string &path) {
  std::vector<std::string> dirs_list{};

  AE_TELE_DEBUG(FsObjRemoved, "Path {}", path);
  for (auto &ItemObjClassData : state_ram_) {
    for (auto &ItemClassData : ItemObjClassData.second) {
      for (auto &ItemVersionData : ItemClassData.second) {
        // Path is "state/version/obj_id/class_id"
        dirs_list.push_back("state/" + std::to_string(ItemVersionData.first) +
                            "/" + ItemObjClassData.first.ToString() + "/" +
                            std::to_string(ItemClassData.first));
      }
    }
  }

  return dirs_list;
}

}  // namespace ae
