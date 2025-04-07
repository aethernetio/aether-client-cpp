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
#include "aether/port/file_systems/file_systems_tele.h"

namespace ae {
  
DriverRam::DriverRam() {}

DriverRam::~DriverRam() {}

void DriverRam::DriverRamRead(const std::string &path,
                              std::vector<std::uint8_t> &data_vector) {

  ae::ObjId obj_id;
  std::uint32_t class_id;
  std::uint8_t version;
  
  // Path is "state/obj_id/class_id/version"

  auto pos1 = path.find("/");
  if(pos1 != std::string::npos){
  auto pos2 = path.find("/", pos1+1);
  if(pos2 != std::string::npos){
  auto pos3 = path.find("/", pos2+1);
  
  obj_id = static_cast<ae::ObjId>(std::stoul(path.substr(pos1, pos2-pos1-1)));
  class_id = static_cast<std::uint32_t>(std::stoul(path.substr(pos2, pos3-pos2-1)));
  version = static_cast<std::uint8_t>(std::stoul(path.substr(pos3, path.length()-pos3-1)));

  auto obj_it = state_.find(obj_id);
  if (obj_it == state_.end()) {
    return;
  }

  auto class_it = obj_it->second.find(class_id);
  if (class_it == obj_it->second.end()) {
    return;
  }

  auto version_it = class_it->second.find(version);
  if (version_it == class_it->second.end()) {
    return;
  }
  data_vector = version_it->second;
  }
  }
}

void DriverRam::DriverRamWrite(const std::string &path,
                               const std::vector<std::uint8_t> &data_vector) {

  ae::ObjId obj_id;
  std::uint32_t class_id;
  std::uint8_t version;

  // Path is "state/obj_id/class_id/version"

  auto pos1 = path.find("/");
  if(pos1 != std::string::npos){
  auto pos2 = path.find("/", pos1+1);
  if(pos2 != std::string::npos){
  auto pos3 = path.find("/", pos2+1);
  
  obj_id = static_cast<ae::ObjId>(std::stoul(path.substr(pos1, pos2-pos1-1)));
  class_id = static_cast<std::uint32_t>(std::stoul(path.substr(pos2, pos3-pos2-1)));
  version = static_cast<std::uint8_t>(std::stoul(path.substr(pos3, path.length()-pos3-1)));

  state_[obj_id][class_id][version] = data_vector;
  }
  }
}

void DriverRam::DriverRamDelete(const std::string &path) {

  ae::ObjId obj_id;
  std::uint32_t class_id;
  std::uint8_t version;

  // Path is "state/obj_id/class_id/version"

  auto pos1 = path.find("/");
  if(pos1 != std::string::npos){
  auto pos2 = path.find("/", pos1+1);
  if(pos2 != std::string::npos){
  auto pos3 = path.find("/", pos2+1);
  
  obj_id = static_cast<ae::ObjId>(std::stoul(path.substr(pos1, pos2-pos1-1)));
  class_id = static_cast<std::uint32_t>(std::stoul(path.substr(pos2, pos3-pos2-1)));
  version = static_cast<std::uint8_t>(std::stoul(path.substr(pos3, path.length()-pos3-1)));
  
  auto it = state_.find(obj_id);
  if (it != state_.end()) {
    AE_TELE_DEBUG(FsObjRemoved, "Removed object {}", obj_id.ToString());
    state_.erase(it);
  } else {
    AE_TELE_ERROR(FsRemoveObjIdNoFound, "Object id={} not found!",
                  obj_id.ToString());
  }
  }
  }
}

std::vector<std::string> DriverRam::DriverRamDir(const std::string &path) {
  std::vector<std::string> dirs_list{};

  ae::ObjId obj_id;
  std::uint32_t class_id;
  std::uint8_t version;
  
  // Path is "state/obj_id/class_id/version"

  auto pos1 = path.find("/");
  if(pos1 != std::string::npos){
  auto pos2 = path.find("/", pos1+1);
  if(pos2 != std::string::npos){
  auto pos3 = path.find("/", pos2+1);
  
  obj_id = static_cast<ae::ObjId>(std::stoul(path.substr(pos1, pos2-pos1-1)));
  class_id = static_cast<std::uint32_t>(std::stoul(path.substr(pos2, pos3-pos2-1)));
  version = static_cast<std::uint8_t>(std::stoul(path.substr(pos3, path.length()-pos3-1)));
  
  auto it = state_.find(obj_id);
  if (it != state_.end()) {
    auto& obj_classes = it->second;
    for (const auto& [class_id, _] : obj_classes) {
      dirs_list.push_back("state/" + obj_id.ToString() + "/" + std::to_string(class_id));
    }
  }
  }
  }

  return dirs_list;
}

}  // namespace ae
