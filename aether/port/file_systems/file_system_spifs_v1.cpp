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

#if defined AE_FILE_SYSTEM_SPIFS_V1_ENABLED

#  include "aether/tele/tele.h"

namespace ae {

FileSystemSpiFsV1Facility::FileSystemSpiFsV1Facility() {
  driver_fs = new DriverSpifs();
  AE_TELED_DEBUG("New FileSystemSpiFsV1 instance created!");
  // driver_fs->DriverSpifsFormat();
}

FileSystemSpiFsV1Facility::~FileSystemSpiFsV1Facility() {
  delete driver_fs;
  AE_TELED_DEBUG("FileSystemSpiFsV1 instance deleted!");
}

std::vector<uint32_t> FileSystemSpiFsV1Facility::Enumerate(
    const ae::ObjId& obj_id) {
  ObjClassData state_;
  std::vector<uint32_t> classes;

  // Reading ObjClassData
  _LoadObjData(state_);

  // Enumerate
  auto it = state_.find(obj_id);
  if (it != state_.end()) {
    auto& obj_classes = it->second;
    AE_TELED_DEBUG("Object id={} found!", obj_id.ToString());
    for (const auto& [class_id, _] : obj_classes) {
      AE_TELED_DEBUG("Add to the classes {}", class_id);
      classes.push_back(class_id);
    }
  }

  return classes;
}

void FileSystemSpiFsV1Facility::Store(const ae::ObjId& obj_id,
                                      std::uint32_t class_id,
                                      std::uint8_t version,
                                      const std::vector<uint8_t>& os) {
  ObjClassData state_;

  // Reading ObjClassData
  _LoadObjData(state_);

  state_[obj_id][class_id][version] = os;

  AE_TELED_DEBUG("Saved state/{}/{}/{} size: {}", std::to_string(version),
                 obj_id.ToString(), class_id, os.size());

  AE_TELED_DEBUG("Object id={} & class id = {} saved!", obj_id.ToString(),
                 class_id);

  // Writing ObjClassData
  _SaveObjData(state_);
}

void FileSystemSpiFsV1Facility::Load(const ae::ObjId& obj_id,
                                     std::uint32_t class_id,
                                     std::uint8_t version,
                                     std::vector<uint8_t>& is) {
  ObjClassData state_;

  // Reading ObjClassData
  _LoadObjData(state_);

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

  AE_TELED_DEBUG("Object id={} & class id = {} version {} loaded!",
                 obj_id.ToString(), class_id, version);
  is = version_it->second;

  AE_TELED_DEBUG("Loaded state/{}/{}/{} size: {}", std::to_string(version),
                 obj_id.ToString(), class_id, is.size());
}

void FileSystemSpiFsV1Facility::Remove(const ae::ObjId& obj_id) {
  ObjClassData state_;

  // Reading ObjClassData
  _LoadObjData(state_);

  auto it = state_.find(obj_id);
  if (it != state_.end()) {
    AE_TELED_DEBUG("Object id={} removed!", obj_id.ToString());
    state_.erase(it);
  } else {
    AE_TELED_WARNING("Object id={} not found!", obj_id.ToString());
  }

  // Writing ObjClassData
  _SaveObjData(state_);
}

void FileSystemSpiFsV1Facility::remove_all() {
  std::string path{"dump"};

  driver_fs->DriverSpifsDelete(path);
}

void FileSystemSpiFsV1Facility::_LoadObjData(ObjClassData& obj_data) {
  auto data_vector = std::vector<std::uint8_t>{};
  std::string path{"dump"};

  VectorReader<PacketSize> vr{data_vector};

  driver_fs->DriverSpifsRead(path, data_vector);
  auto is = imstream{vr};
  // add oj data
  is >> obj_data;
}

void FileSystemSpiFsV1Facility::_SaveObjData(ObjClassData& obj_data) {
  auto data_vector = std::vector<std::uint8_t>{};
  std::string path{"dump"};

  VectorWriter<PacketSize> vw{data_vector};
  auto os = omstream{vw};
  // add file data
  os << obj_data;

  driver_fs->DriverSpifsWrite(path, data_vector);
}

}  // namespace ae

#endif  // AE_FILE_SYSTEM_SPIFS_V1_ENABLED
