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

#include "aether/port/file_systems/file_system_header.h"

#if AE_FILE_SYSTEM_HEADER_ENABLED==1

#  include "aether/transport/low_level/tcp/data_packet_collector.h"
#  include "aether/port/file_systems/file_systems_tele.h"

namespace ae {

FileSystemHeaderFacility::FileSystemHeaderFacility(
    const std::string& header_file,
    enum DriverFsType fs_driver_type_destination)
    : path_{header_file} {
  driver_sync_fs_ = std::make_unique<DriverSync>(DriverFsType::kDriverHeader,
                                                 fs_driver_type_destination);
  AE_TELED_DEBUG("New FileSystemHeader instance created!");
}

FileSystemHeaderFacility::~FileSystemHeaderFacility() {
  AE_TELED_DEBUG("FileSystemHeader instance deleted!");
}

std::vector<uint32_t> FileSystemHeaderFacility::Enumerate(const ObjId& obj_id) {
  ObjClassData state_;
  std::vector<uint32_t> classes;

  // Reading ObjClassData
  LoadObjData_(state_);

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

void FileSystemHeaderFacility::Store(const ObjId& obj_id,
                                     std::uint32_t class_id,
                                     std::uint8_t version,
                                     const std::vector<uint8_t>& os) {
  ObjClassData state_;

  // Reading ObjClassData
  LoadObjData_(state_);

  state_[obj_id][class_id][version] = os;

  AE_TELED_DEBUG("Saved state/{}/{}/{} size: {}", std::to_string(version),
                 obj_id.ToString(), class_id, os.size());

  AE_TELED_DEBUG("Object id={} & class id = {} saved!", obj_id.ToString(),
                 class_id);

  // Writing ObjClassData
  SaveObjData_(state_);

  std::string path{};

  path = "state/" + std::to_string(version) + "/" + obj_id.ToString() + "/" +
         std::to_string(class_id);

  // For FS syncronization
  driver_sync_fs_->DriverWrite(path, os);

  AE_TELE_DEBUG(
      FsObjSaved, "Saved object id={}, class id={}, version={}, size={}",
      obj_id.ToString(), class_id, static_cast<int>(version), os.size());
}

void FileSystemHeaderFacility::Load(const ObjId& obj_id, std::uint32_t class_id,
                                    std::uint8_t version,
                                    std::vector<uint8_t>& is) {
  ObjClassData state_;

  std::string path{};

  path = "state/" + std::to_string(version) + "/" + obj_id.ToString() + "/" +
         std::to_string(class_id);

  // For FS syncronization
  driver_sync_fs_->DriverRead(path, is);

  // Reading ObjClassData
  LoadObjData_(state_);

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

  is = version_it->second;

  AE_TELE_DEBUG(
      FsObjLoaded, "Loaded object id={}, class id={}, version={}, size={}",
      obj_id.ToString(), class_id, static_cast<int>(version), is.size());
}

void FileSystemHeaderFacility::Remove(const ObjId& obj_id) {
  ObjClassData state_;

  // Reading ObjClassData
  LoadObjData_(state_);

  auto it = state_.find(obj_id);
  if (it != state_.end()) {
    AE_TELE_DEBUG(FsObjRemoved, "Object id={} removed!", obj_id.ToString());
    state_.erase(it);
  } else {
    AE_TELED_WARNING("Object id={} not found!", obj_id.ToString());
  }

  // Writing ObjClassData
  SaveObjData_(state_);
}

#  if defined AE_DISTILLATION
void FileSystemHeaderFacility::CleanUp() {
  driver_sync_fs_->DriverDelete(path_);
}
#  endif

void FileSystemHeaderFacility::LoadObjData_(ObjClassData& obj_data) {
#  if (defined(ESP_PLATFORM) || !defined(AE_DISTILLATION))
#    if defined FS_INIT
  auto data_vector = std::vector<std::uint8_t>{init_fs.begin(), init_fs.end()};
#    else
  auto data_vector = std::vector<std::uint8_t>{};
#    endif

  VectorReader<PacketSize> vr{data_vector};

  auto is = imstream{vr};
  // add oj data
  is >> obj_data;
#  else
  std::vector<std::uint8_t> data_vector{};

  VectorReader<PacketSize> vr{data_vector};

  driver_sync_fs_->DriverRead(path_, data_vector);
  auto is = imstream{vr};
  // add obj data
  is >> obj_data;
#  endif
}

void FileSystemHeaderFacility::SaveObjData_(ObjClassData& obj_data) {
#  if (defined(ESP_PLATFORM) || !defined(AE_DISTILLATION))
#    if defined FS_INIT
  auto data_vector = std::vector<std::uint8_t>{init_fs.begin(), init_fs.end()};
#    else
  auto data_vector = std::vector<std::uint8_t>{};
#    endif

  VectorWriter<PacketSize> vw{data_vector};

  auto os = omstream{vw};
  // add file data
  os << obj_data;

#  else
  std::vector<std::uint8_t> data_vector{};

  VectorWriter<PacketSize> vw{data_vector};
  auto os = omstream{vw};
  // add file data
  os << obj_data;

  driver_sync_fs_->DriverWrite(path_, data_vector);
#  endif
}

}  // namespace ae

#endif  // AE_FILE_SYSTEM_HEADER_ENABLED
