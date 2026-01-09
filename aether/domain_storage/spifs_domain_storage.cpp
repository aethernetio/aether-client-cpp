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

#include "aether/domain_storage/spifs_domain_storage.h"

#if defined AE_SPIFS_DOMAIN_STORAGE_ENABLED

#  include <algorithm>

#  include "sys/stat.h"

#  include "esp_err.h"
#  include "esp_spiffs.h"
#  include "spiffs_config.h"

#  include "aether/mstream.h"
#  include "aether/mstream_buffers.h"

#  include "aether/domain_storage/domain_storage_tele.h"

namespace ae {

class FileWriter : public IDomainStorageWriter {
 public:
  explicit FileWriter(FILE* f) : file{f} {}

  virtual ~FileWriter() { fclose(file); }

  void write(void const* data, std::size_t size) override {
    fwrite(data, 1, size, file);
  }

  FILE* file;
};

class SpiFsSotorageWriter final : public FileWriter {
 public:
  explicit SpiFsSotorageWriter(FILE* f, DomainQuery q)
      : FileWriter{f}, query{std::move(q)} {}

  ~SpiFsSotorageWriter() {
    AE_TELE_DEBUG(
        kSpifsDsObjSaved, "Saved object id={}, class id={}, version={}",
        query.id.ToString(), query.class_id, static_cast<int>(query.version));
  }

  DomainQuery query;
};

class SpiFsSotorageReader final : public IDomainStorageReader {
 public:
  explicit SpiFsSotorageReader(FILE* f) : file{f} {}
  ~SpiFsSotorageReader() { fclose(file); }

  void read(void* data, std::size_t size) override {
    fread(data, 1, size, file);
  }

  ReadResult result() const override { return ReadResult::kYes; }
  void result(ReadResult) override {}

  FILE* file;
};

SpiFsDomainStorage::SpiFsDomainStorage() {
  InitFs();
  InitState();
}

SpiFsDomainStorage::~SpiFsDomainStorage() { DeInitFs(); }

std::unique_ptr<IDomainStorageWriter> SpiFsDomainStorage::Store(
    DomainQuery const& query) {
  // open file
  auto file_path = Format("{}/{}/{}/{}", kBasePath, query.id.ToString(),
                          query.class_id, static_cast<int>(query.version));
  FILE* file = fopen(file_path.c_str(), "w");
  if (file == nullptr) {
    AE_TELED_ERROR("Failed to open file {} for writing.", file_path);
    return {};
  }

  // register query in object map
  object_map_[query.id][query.class_id].emplace_back(query.version);
  SyncState();

  return std::make_unique<SpiFsSotorageWriter>(file, query);
}

ClassList SpiFsDomainStorage::Enumerate(ObjId const& obj_id) {
  auto obj_it = object_map_.find(obj_id);
  if (obj_it == std::end(object_map_)) {
    AE_TELE_ERROR(kSpifsDsEnumObjIdNotFound, "Obj not found {}",
                  obj_id.ToString());
    return {};
  }

  ClassList classes;
  for (auto const& [class_id, _] : obj_it->second) {
    classes.emplace_back(class_id);
  }
  AE_TELE_DEBUG(kSpifsDsEnumerated, "Enumerated for obj {} classes {}",
                obj_id.ToString(), classes);
  return classes;
}

DomainLoad SpiFsDomainStorage::Load(DomainQuery const& query) {
  auto obj_map_it = object_map_.find(query.id);
  if (obj_map_it == std::end(object_map_)) {
    AE_TELE_ERROR(kSpifsDsLoadObjIdNoFound,
                  "Unable to find object id={}, class id={}, version={}",
                  query.id.ToString(), query.class_id,
                  static_cast<int>(query.version));
    return {DomainLoadResult::kEmpty, {}};
  }
  if (obj_map_it->second.empty()) {
    return {DomainLoadResult::kRemoved, {}};
  }

  auto class_map_it = obj_map_it->second.find(query.class_id);
  if (class_map_it == std::end(obj_map_it->second)) {
    AE_TELE_ERROR(kSpifsDsLoadObjClassIdNotFound,
                  "Unable to find object id={}, class id={}, version={}",
                  query.id.ToString(), query.class_id,
                  static_cast<int>(query.version));
    return {DomainLoadResult::kEmpty, {}};
  }
  auto version_it = std::find(std::begin(class_map_it->second),
                              std::end(class_map_it->second), query.version);
  if (version_it == std::end(class_map_it->second)) {
    AE_TELE_ERROR(kSpifsDsLoadObjVersionNotFound,
                  "Unable to find object id={}, class id={}, version={}",
                  query.id.ToString(), query.class_id,
                  static_cast<int>(query.version));
    return {DomainLoadResult::kEmpty, {}};
  }

  // open file
  auto file_path = Format("{}/{}/{}/{}", kBasePath, query.id.ToString(),
                          query.class_id, static_cast<int>(query.version));
  FILE* file = fopen(file_path.c_str(), "r");
  if (file == nullptr) {
    AE_TELED_ERROR("Failed to open file {} for reading.", file_path);
    return {};
  }

  AE_TELE_DEBUG(
      kSpifsDsObjLoaded, "Loaded object id={}, class id={}, version={}",
      query.id.ToString(), query.class_id, static_cast<int>(query.version));

  return {DomainLoadResult::kLoaded,
          std::make_unique<SpiFsSotorageReader>(file)};
}

void SpiFsDomainStorage::Remove(const ae::ObjId& obj_id) {
  auto obj_map_it = object_map_.find(obj_id);
  if (obj_map_it == std::end(object_map_)) {
    object_map_.emplace(obj_id.id(), ClassMap{});
    return;
  }

  for (auto& [class_id, class_data] : obj_map_it->second) {
    for (auto version : class_data) {
      // remove the file
      auto file_path = Format("{}/{}/{}/{}", kBasePath, obj_id.ToString(),
                              class_id, static_cast<int>(version));
      unlink(file_path.c_str());
    }
  }
  obj_map_it->second.clear();
  AE_TELE_DEBUG(kSpifsDsObjRemoved, "Removed object {}", obj_id.ToString());
  SyncState();
}

void SpiFsDomainStorage::CleanUp() {
  for (auto const& [obj_id, obj_map_data] : object_map_) {
    for (auto const& [class_id, class_data] : obj_map_data) {
      for (auto version : class_data) {
        // remove the file
        auto file_path = Format("{}/{}/{}/{}", kBasePath, obj_id.ToString(),
                                class_id, static_cast<int>(version));
        unlink(file_path.c_str());
      }
    }
  }
  object_map_.clear();
  SyncState();
}

void SpiFsDomainStorage::InitFs() {
  esp_vfs_spiffs_conf_t conf = {kBasePath.data(), kPartition.data(), 128, true};

  // Use settings defined above to initialize and mount SPIFFS filesystem.
  // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
  esp_err_t ret = esp_vfs_spiffs_register(&conf);

  if (ret != ESP_ERR_INVALID_STATE) {  // FS already is initialized
    if (ret != ESP_OK) {
      AE_TELE_ERROR(kSpifsDsStorageMountError, "Init SPIFS v1 error {}",
                    [ret]() -> std::string {
                      if (ret == ESP_FAIL) {
                        return "Failed to mount or format filesystem";
                      } else if (ret == ESP_ERR_NOT_FOUND) {
                        return "Failed to find SPIFFS partition";
                      } else {
                        return Format("Failed to initialize SPIFFS ({})",
                                      esp_err_to_name(ret));
                      }
                    }());
    }
    return;
  }

  std::size_t total = 0;
  std::size_t used = 0;
  ret = esp_spiffs_info(kPartition.data(), &total, &used);
  if (ret != ESP_OK) {
    AE_TELE_ERROR(kSpifsDsStorageInitError,
                  "Failed to get SPIFFS partition information ({})",
                  esp_err_to_name(ret));
  } else {
    AE_TELE_INFO(kSpifsDsStorageInit, "Partition size: total: {}, used: {}",
                 total, used);
  }
}

void SpiFsDomainStorage::DeInitFs() {
  esp_vfs_spiffs_unregister(kPartition.data());
}

void SpiFsDomainStorage::InitState() {
  auto file = fopen(kObjectMapPath.data(), "r");
  if (!file) {
    AE_TELED_DEBUG("File {} does not exists ", kObjectMapPath);
    return;
  }

  auto file_reader = SpiFsSotorageReader{file};
  imstream is{file_reader};
  is >> object_map_;
}

void SpiFsDomainStorage::SyncState() {
  auto file = fopen(kObjectMapPath.data(), "w");
  if (!file) {
    AE_TELED_ERROR("Failed to open file {} for writing.", kObjectMapPath);
    return;
  }
  auto file_writer = FileWriter{file};
  omstream os{file_writer};
  os << object_map_;
}
}  // namespace ae

#endif  // AE_SPIFS_DOMAIN_STORAGE_ENABLED
