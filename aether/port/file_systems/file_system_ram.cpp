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

#include "aether/port/file_systems/file_system_ram.h"

#if defined AE_FILE_SYSTEM_RAM_ENABLED

#  include "aether/transport/low_level/tcp/data_packet_collector.h"

#  include "aether/tele/tele.h"

namespace ae {
/*
 *\brief Class constructor.
 *\param[in] void.
 *\return void.
 */
FileSystemRamFacility::FileSystemRamFacility() {
  driver_fs = new DriverHeader();
  AE_TELED_DEBUG("New FileSystemRam instance created!");
}

/*
 * \brief Class destructor.
 * \param[in] void.
 * \return void.
 */
FileSystemRamFacility::~FileSystemRamFacility() {
  AE_TELED_DEBUG("FileSystemRam instance deleted!");
}

std::vector<uint32_t> FileSystemRamFacility::Enumerate(
    const ae::ObjId& obj_id) {
  std::vector<uint32_t> classes;

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

void FileSystemRamFacility::Store(const ae::ObjId& obj_id,
                                  std::uint32_t class_id, std::uint8_t version,
                                  const std::vector<uint8_t>& os) {
  state_[obj_id][class_id][version] = os;

  AE_TELED_DEBUG("Saved state/{}/{}/{} size: {}", std::to_string(version),
                 obj_id.ToString(), class_id, os.size());

  AE_TELED_DEBUG("Object id={} & class id = {} saved!", obj_id.ToString(),
                 class_id);
}

void FileSystemRamFacility::Load(const ae::ObjId& obj_id,
                                 std::uint32_t class_id, std::uint8_t version,
                                 std::vector<uint8_t>& is) {
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
                 obj_id.ToString(), class_id, std::to_string(version));
  is = version_it->second;

  AE_TELED_DEBUG("Loaded state/{}/{}/{} size: {}", std::to_string(version),
                 obj_id.ToString(), class_id, is.size());
}

void FileSystemRamFacility::Remove(const ae::ObjId& obj_id) {
  auto it = state_.find(obj_id);
  if (it != state_.end()) {
    AE_TELED_DEBUG("Object id={} removed!", obj_id.ToString());
    state_.erase(it);
  } else {
    AE_TELED_WARNING("Object id={} not found!", obj_id.ToString());
  }
}

void FileSystemRamFacility::remove_all() {
  state_.clear();
  AE_TELED_DEBUG("All objects have been removed!");
}

void FileSystemRamFacility::out_header() {
  std::string path{"config/file_system_init.h"};
  auto data_vector = std::vector<std::uint8_t>{};
  VectorWriter<PacketSize> vw{data_vector};
  auto os = omstream{vw};
  // add file data
  os << state_;

  driver_fs->DriverHeaderWrite(path, data_vector);
}
}  // namespace ae

#endif  // AE_FILE_SYSTEM_RAM_ENABLED
