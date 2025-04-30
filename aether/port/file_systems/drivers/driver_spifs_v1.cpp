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

#if (defined(ESP_PLATFORM))

#  include "aether/port/file_systems/drivers/driver_spifs_v1.h"
#  include "aether/port/file_systems/drivers/driver_functions.h"
#  include "aether/port/file_systems/file_systems_tele.h"
#  include "aether/tele/tele.h"

namespace ae {

DriverSpifsV1::DriverSpifsV1() {
  if (initialized_ == false) {
    if (DriverInit_() == ESP_OK) initialized_ = true;
  }
}

DriverSpifsV1::~DriverSpifsV1() {
  DriverDeinit_();
  initialized_ = false;
}

void DriverSpifsV1::DriverRead(const std::string &path,
                               std::vector<std::uint8_t> &data_vector,
                               bool sync) {
  if (sync == false) {
    ae::PathStructure path_struct{};

    // Reading ObjClassData
    LoadObjData_(state_spifs_);

    path_struct = GetPathStructure(path);

    auto obj_it = state_spifs_.find(path_struct.obj_id);
    if (obj_it == state_spifs_.end()) {
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

void DriverSpifsV1::DriverWrite(const std::string &path,
                                const std::vector<std::uint8_t> &data_vector) {
  ae::PathStructure path_struct{};

  // Reading ObjClassData
  LoadObjData_(state_spifs_);

  path_struct = GetPathStructure(path);

  state_spifs_[path_struct.obj_id][path_struct.class_id][path_struct.version] =
      data_vector;

  // Writing ObjClassData
  SaveObjData_(state_spifs_);
}

void DriverSpifsV1::DriverDelete(const std::string &path) {
  ae::PathStructure path_struct{};

  // Reading ObjClassData
  LoadObjData_(state_spifs_);

  path_struct = GetPathStructure(path);

  auto it = state_spifs_.find(path_struct.obj_id);
  if (it != state_spifs_.end()) {
    AE_TELE_DEBUG(FsObjRemoved, "Removed object {}",
                  path_struct.obj_id.ToString());
    state_spifs_.erase(it);
  } else {
    AE_TELE_ERROR(FsRemoveObjIdNoFound, "Object id={} not found!",
                  path_struct.obj_id.ToString());
  }

  // Writing ObjClassData
  SaveObjData_(state_spifs_);
}

std::vector<std::string> DriverSpifsV1::DriverDir(const std::string &path) {
  std::vector<std::string> dirs_list{};

  // Reading ObjClassData
  LoadObjData_(state_spifs_);

  AE_TELE_DEBUG(FsObjRemoved, "Path {}", path);
  for (auto &ItemObjClassData : state_spifs_) {
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

void DriverSpifsV1::DriverFormat() {
  esp_err_t err = esp_spiffs_format(kPartition);

  if (err != ESP_OK) {
    AE_TELED_ERROR("Failed to format SPIFFS ({})", esp_err_to_name(err));
  }
}

esp_err_t DriverSpifsV1::DriverInit_() {
  esp_vfs_spiffs_conf_t conf = {.base_path = kBasePath,
                                .partition_label = kPartition,
                                .max_files = 128,
                                .format_if_mount_failed = true};

  // Use settings defined above to initialize and mount SPIFFS filesystem.
  // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
  esp_err_t ret = esp_vfs_spiffs_register(&conf);

  if (ret != ESP_ERR_INVALID_STATE)  // FS alredy is initialized
  {
    if (ret != ESP_OK) {
      if (ret == ESP_FAIL) {
        AE_TELED_ERROR("Failed to mount or format filesystem");
      } else if (ret == ESP_ERR_NOT_FOUND) {
        AE_TELED_ERROR("Failed to find SPIFFS partition");
      } else {
        AE_TELED_ERROR("Failed to initialize SPIFFS ({})",
                       esp_err_to_name(ret));
      }
      return ret;
    }
  }

  size_t total = 0, used = 0;
  ret = esp_spiffs_info(kPartition, &total, &used);
  if (ret != ESP_OK) {
    AE_TELED_ERROR("Failed to get SPIFFS partition information ({})",
                   esp_err_to_name(ret));
  } else {
    AE_TELED_INFO("Partition size: total: {}, used: {}", total, used);
  }
  return ESP_OK;
}

void DriverSpifsV1::DriverDeinit_() { esp_vfs_spiffs_unregister(kPartition); }

void DriverSpifsV1::LoadObjData_(ObjClassData &obj_data) {
  auto data_vector = std::vector<std::uint8_t>{};
  std::string path{"dump"};

  VectorReader<PacketSize> vr{data_vector};

  DriverRead_(path, data_vector);
  auto is = imstream{vr};
  // add oj data
  is >> obj_data;
}

void DriverSpifsV1::SaveObjData_(ObjClassData &obj_data) {
  auto data_vector = std::vector<std::uint8_t>{};
  std::string path{"dump"};

  VectorWriter<PacketSize> vw{data_vector};
  auto os = omstream{vw};
  // add file data
  os << obj_data;

  DriverWrite_(path, data_vector);
}

void DriverSpifsV1::DriverRead_(const std::string &path,
                                std::vector<std::uint8_t> &data_vector) {
  size_t bytes_read;
  unsigned int file_size = 0;
  std::string res_path{};

  if (!initialized_) return;
  res_path = kBasePath + std::string("/") + path;

  AE_TELE_DEBUG(FsObjLoaded, "Opening file {} for read.", res_path);
  FILE *file = fopen(res_path.c_str(), "r");
  if (file == nullptr) {
    AE_TELED_ERROR("Failed to open file {} for reading.", res_path);
  } else {
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    AE_TELE_DEBUG(FsObjLoaded, "File length {}", file_size);

    data_vector.resize(file_size);

    bytes_read = fread(data_vector.data(), 1, file_size, file);
    AE_TELE_DEBUG(FsObjLoaded, "Reading from the file {}. Bytes read {}",
                  res_path, bytes_read);
    /*for(std::uint8_t& i : data_vector)
    {
      auto res = static_cast<std::uint32_t>(i);
      AE_TELE_DEBUG(FsObjLoaded, "Data read is {}", res);
    }*/

    fclose(file);
  }
}

void DriverSpifsV1::DriverWrite_(const std::string &path,
                                 const std::vector<std::uint8_t> &data_vector) {
  size_t bytes_write;
  std::string res_path{};

  if (!initialized_) return;
  res_path = kBasePath + std::string("/") + path;

  AE_TELE_DEBUG(FsObjSaved, "Opening file {} for write.", res_path);
  FILE *file = fopen(res_path.c_str(), "w");
  if (file == nullptr) {
    AE_TELE_ERROR(FsObjSaved, "Failed to open file {} for writing.", res_path);
  } else {
    bytes_write = fwrite(data_vector.data(), 1, data_vector.size(), file);
    AE_TELE_INFO(FsObjSaved, "Writing to the file {}. Bytes write {}", res_path,
                 bytes_write);
    /*for(const std::uint8_t& i : data_vector)
    {
      auto res = static_cast<std::uint32_t>(i);
      AE_TELE_DEBUG(FsObjSaved, "Data write is {}", res);
    }*/

    fclose(file);
  }
}

}  // namespace ae

#endif  // (defined(ESP_PLATFORM))
