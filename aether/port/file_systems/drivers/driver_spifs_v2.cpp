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

#include "aether/port/file_systems/drivers/driver_spifs_v2.h"
#include "aether/port/file_systems/file_systems_tele.h"
#include "aether/tele/tele.h"

#if (defined(ESP_PLATFORM))

namespace ae {
DriverSpifsV2::DriverSpifsV2(DriverFsType fs_driver_type)
    : DriverBase(fs_driver_type) {
  if (initialized_ == false) {
    if (DriverInit() == ESP_OK) initialized_ = true;
  }
}

DriverSpifsV2::~DriverSpifsV2() {
  DriverDeinit();
  initialized_ = false;
}

void DriverSpifsV2::DriverRead(const std::string &path,
                               std::vector<std::uint8_t> &data_vector,
                               bool sync) {
  size_t bytes_read;
  unsigned int file_size = 0;
  std::string res_path{};

  if (!initialized_) return;
  res_path = kBasePath + std::string("/") + path;

  AE_TELE_DEBUG(FsObjLoaded, "Opening file {} for read.", res_path);
  FILE *file = fopen(res_path.c_str(), "r");
  if (file == nullptr) {
    AE_TELE_ERROR(FsObjLoaded, "Failed to open file {} for reading.", res_path);
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

void DriverSpifsV2::DriverWrite(const std::string &path,
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

void DriverSpifsV2::DriverDelete(const std::string &path) {
  struct stat status;
  std::string res_path{};

  if (!initialized_) return;
  res_path = kBasePath + std::string("/") + path;

  AE_TELED_DEBUG("Opening file {} for delete.", res_path);
  // Check if destination file exists before renaming
  if (stat(res_path.c_str(), &status) == 0) {
    // Delete it if it exists
    unlink(res_path.c_str());
    AE_TELED_DEBUG("File {} deleted", res_path);
  }
}

std::vector<std::string> DriverSpifsV2::DriverDir(const std::string &path) {
  struct dirent *de;
  bool read_dir{true};
  std::vector<std::string> dirs_list{};
  std::string res_path{};

  if (!initialized_) return dirs_list;
  res_path = kBasePath + std::string("/") + path;

  DIR *dir = opendir(res_path.c_str());
  if (dir == nullptr) {
    AE_TELED_DEBUG("Error, directory not found!");
  } else {
    while (read_dir) {
      de = readdir(dir);
      if (de == nullptr) {
        read_dir = false;
      } else {
        res_path = std::string(de->d_name);
        dirs_list.push_back(res_path);
      }
    }
    AE_TELED_DEBUG("Found {} directories", dirs_list.size());
  }

  return dirs_list;
}

void DriverSpifsV2::DriverFormat() {
  esp_err_t err = esp_spiffs_format(kPartition);

  if (err != ESP_OK) {
    AE_TELED_ERROR("Failed to format SPIFFS ({})", esp_err_to_name(err));
  }
}

esp_err_t DriverSpifsV2::DriverInit() {
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

void DriverSpifsV2::DriverDeinit() { esp_vfs_spiffs_unregister(kPartition); }

}  // namespace ae

#endif  // (defined(ESP_PLATFORM))
