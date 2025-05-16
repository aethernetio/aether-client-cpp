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
#  include "aether/transport/low_level/tcp/data_packet_collector.h"
#  include "aether/port/file_systems/file_systems_tele.h"
#  include "aether/tele/tele.h"

namespace ae {

DriverSpifsV1::DriverSpifsV1(DriverFsType fs_driver_type)
    : DriverBase(fs_driver_type) {
  if (initialized_ == false) {
    if (DriverInit() == ESP_OK) initialized_ = true;
  }
}

DriverSpifsV1::~DriverSpifsV1() {
  DriverDeinit();
  initialized_ = false;
}

void DriverSpifsV1::DriverRead(const PathStructure &path,
                               std::vector<std::uint8_t> &data_vector,
                               bool sync) {
  if (!sync) {
    // Reading ObjClassData
    LoadObjData(state_spifs_);

    auto obj_it = state_spifs_.find(path.obj_id);
    if (obj_it == state_spifs_.end()) {
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

void DriverSpifsV1::DriverWrite(const PathStructure &path,
                                const std::vector<std::uint8_t> &data_vector) {
  // Reading ObjClassData
  LoadObjData(state_spifs_);

  state_spifs_[path.obj_id][path.class_id][path.version] =
      data_vector;

  // Writing ObjClassData
  SaveObjData(state_spifs_);
}

void DriverSpifsV1::DriverDelete(const PathStructure &path) {
  // Reading ObjClassData
  LoadObjData(state_spifs_);

  auto it = state_spifs_.find(path.obj_id);
  if (it != state_spifs_.end()) {
    AE_TELE_DEBUG(FsObjRemoved, "Removed object {}",
                  path.obj_id.ToString());
    state_spifs_.erase(it);
  } else {
    AE_TELE_ERROR(FsRemoveObjIdNoFound, "Object id={} not found!",
                  path.obj_id.ToString());
  }

  // Writing ObjClassData
  SaveObjData(state_spifs_);
}

std::vector<PathStructure> DriverSpifsV1::DriverDir(const PathStructure &path) {
  PathStructure res_path{};
  std::vector<PathStructure> dirs_list{};

  // Reading ObjClassData
  LoadObjData(state_spifs_);

  // Path is "state/version/obj_id/class_id"
  AE_TELE_DEBUG(FsDir, "Path {}", GetPathString(GetPathString(path));
  for (auto &ItemObjClassData : state_spifs_) {
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

void DriverSpifsV1::DriverFormat() {
  esp_err_t err = esp_spiffs_format(kPartition);

  if (err != ESP_OK) {
    AE_TELE_ERROR(FsFormat, "Failed to format SPIFFS ({})",
                  esp_err_to_name(err));
  }
}

esp_err_t DriverSpifsV1::DriverInit() {
  esp_vfs_spiffs_conf_t conf = {.base_path = kBasePath,
                                .partition_label = kPartition,
                                .max_files = 128,
                                .format_if_mount_failed = true};

  // Use settings defined above to initialize and mount SPIFFS filesystem.
  // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
  esp_err_t ret = esp_vfs_spiffs_register(&conf);

  if (ret != ESP_ERR_INVALID_STATE) {  // FS alredy is initialized
    if (ret != ESP_OK) {
      if (ret == ESP_FAIL) {
        AE_TELE_ERROR(FsDriverInit, "Failed to mount or format filesystem");
      } else if (ret == ESP_ERR_NOT_FOUND) {
        AE_TELE_ERROR(FsDriverInit, "Failed to find SPIFFS partition");
      } else {
        AE_TELE_ERROR(FsDriverInit, "Failed to initialize SPIFFS ({})",
                      esp_err_to_name(ret));
      }
      return ret;
    }
  }

  size_t total = 0, used = 0;
  ret = esp_spiffs_info(kPartition, &total, &used);
  if (ret != ESP_OK) {
    AE_TELE_ERROR(FsDriverInit,
                  "Failed to get SPIFFS partition information ({})",
                  esp_err_to_name(ret));
  } else {
    AE_TELE_INFO(FsDriverInit, "Partition size: total: {}, used: {}", total,
                 used);
  }
  return ESP_OK;
}

void DriverSpifsV1::DriverDeinit() { esp_vfs_spiffs_unregister(kPartition); }

void DriverSpifsV1::LoadObjData(ObjClassData &obj_data) {
  auto data_vector = std::vector<std::uint8_t>{};
  std::string path{"dump"};

  VectorReader<PacketSize> vr{data_vector};

  DriverReadHlp(path, data_vector);
  auto is = imstream{vr};
  // add oj data
  is >> obj_data;
}

void DriverSpifsV1::SaveObjData(ObjClassData &obj_data) {
  auto data_vector = std::vector<std::uint8_t>{};
  std::string path{"dump"};

  VectorWriter<PacketSize> vw{data_vector};
  auto os = omstream{vw};
  // add file data
  os << obj_data;

  DriverWriteHlp(path, data_vector);
}

void DriverSpifsV1::DriverReadHlp(const std::string &path,
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

    fclose(file);
  }
}

void DriverSpifsV1::DriverWriteHlp(
    const std::string &path, const std::vector<std::uint8_t> &data_vector) {
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

    fclose(file);
  }
}

}  // namespace ae

#endif  // (defined(ESP_PLATFORM))
