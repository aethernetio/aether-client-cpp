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

#  include <cstring>

#  include "sys/stat.h"

#  include "esp_err.h"
#  include "esp_spiffs.h"
#  include "spiffs_config.h"

#  include "aether-miscpp/crc.h"
#  include "aether-miscpp/serialization/binary_archive.h"

#  include "aether/domain_storage/domain_storage_tele.h"
#  include "aether/ae_exp_diag.h"
#  include "aether/ae_exp_save_stats.h"

namespace ae {
class SpiFsSotorageWriter final : public IDomainStorageWriter {
 public:
  explicit SpiFsSotorageWriter(SpiFsDomainStorage& storage,
                               std::string file_path, DomainQuery q)
      : storage_{&storage},
        file_path{std::move(file_path)},
        query{std::move(q)} {}

  ~SpiFsSotorageWriter() override {
#  if defined(AE_EXP_DIAG)
    auto const t0 = AeExpNowUs();
#  endif
    AeExpSaveInc(&AeExpSaveSample::objects_serialized);
    AeExpSaveAdd(&AeExpSaveSample::serialized_bytes,
                 static_cast<std::uint32_t>(buffer.size()));

    auto const crc_t0 = AeExpSaveNowUs();
    auto const crc = CalcBufferCrc();
    AeExpSaveAdd(&AeExpSaveSample::crc_time_us,
                 static_cast<std::uint32_t>(AeExpSaveNowUs() - crc_t0));
    AeExpSaveInc(&AeExpSaveSample::crc_count);

    if (!storage_->ObjectCrcChanged(query, crc)) {
      AE_EXP_SAVE("crc_skip", "path=%s bytes=%zu obj_id=%lu", file_path.c_str(),
                  buffer.size(), static_cast<unsigned long>(query.id.id()));
      AeExpSaveInc(&AeExpSaveSample::crc_skip_count);
      AE_TELED_DEBUG(
          "For object id={}, class id={}, version={} crc is the same, not "
          "update data",
          query.id.id(), query.class_id, static_cast<int>(query.version));
      return;
    }

    AeExpSaveInc(&AeExpSaveSample::changed_object_count);
    AE_EXP_SAVE("write_begin", "path=%s bytes=%zu obj_id=%lu",
                file_path.c_str(), buffer.size(),
                static_cast<unsigned long>(query.id.id()));

    auto const open_t0 = AeExpSaveNowUs();
    FILE* file = fopen(file_path.c_str(), "w");
    AeExpSaveAdd(&AeExpSaveSample::object_open_us,
                 static_cast<std::uint32_t>(AeExpSaveNowUs() - open_t0));
    if (file == nullptr) {
      AE_TELED_ERROR("Failed to open file {} for writing.", file_path);
      AeExpSaveMarkFail();
      return;
    }

    auto const write_t0 = AeExpSaveNowUs();
    auto res = fwrite(buffer.data(), 1, buffer.size(), file);
    AeExpSaveAdd(&AeExpSaveSample::object_fwrite_us,
                 static_cast<std::uint32_t>(AeExpSaveNowUs() - write_t0));

    auto const close_t0 = AeExpSaveNowUs();
    fclose(file);
    AeExpSaveAdd(&AeExpSaveSample::object_close_us,
                 static_cast<std::uint32_t>(AeExpSaveNowUs() - close_t0));

    if (res != buffer.size()) {
      AE_TELED_ERROR("Failed to write data to file {}", file_path);
      AeExpSaveMarkFail();
      return;
    }

    AeExpSaveInc(&AeExpSaveSample::object_file_write_count);
    AeExpSaveAdd(&AeExpSaveSample::object_file_bytes,
                 static_cast<std::uint32_t>(buffer.size()));

    // Commit CRC into object_map only after a successful object write.
    storage_->ConfirmObjectWritten(query, crc);

#  if defined(AE_EXP_DIAG)
    AE_EXP_SAVE("write_end", "path=%s bytes=%zu duration_us=%lld",
                file_path.c_str(), buffer.size(),
                static_cast<long long>(AeExpNowUs() - t0));
#  endif

    AE_TELE_DEBUG(kSpifsDsObjSaved,
                  "Saved object id={}, class id={}, version={}, data size={}",
                  query.id.id(), query.class_id,
                  static_cast<int>(query.version), buffer.size());
  }

  seri::SeriResult Write(seri::SizeWriteTag data) override {
    auto const size = static_cast<std::uint32_t>(data.size);
    return Write(seri::DataTag{size});
  }

  seri::SeriResult Write(seri::DataWriteTag data) override {
    auto const* p = static_cast<std::uint8_t const*>(data.data);
    buffer.insert(std::end(buffer), p, p + data.size);
    return Ok{seri::good};
  }

 private:
  std::uint32_t CalcBufferCrc() {
    return crc32::from_buffer(buffer.data(), buffer.size()).value;
  }

  SpiFsDomainStorage* storage_;
  std::string file_path;
  DomainQuery query;
  std::vector<std::uint8_t> buffer;
};

class SpiFsSotorageReader final : public IDomainStorageReader {
 public:
  explicit SpiFsSotorageReader(FILE* f) {
    constexpr auto kRead = 256;
    std::size_t off = 0;
    while (true) {
      buffer.resize(off + kRead);
      auto res = fread(buffer.data() + off, 1, kRead, f);
      off += res;
      if (res < kRead) {
        break;
      }
    }
    buffer.resize(off);
    AE_TELED_DEBUG("SpiFsSotorageReader loaded {} bytes", buffer.size());

    fclose(f);
  }

  seri::SeriResult Read(seri::SizeReadTag data) override {
    std::uint32_t size{};
    if (auto res = Read(seri::DataTag{size}); !res) {
      return res;
    }
    data.size = static_cast<std::size_t>(size);
    return Ok{seri::good};
  }

  seri::SeriResult Read(seri::DataReadTag data) override {
    if ((offset + data.size) > buffer.size()) {
      return Error{seri::read_eof};
    }
    std::memcpy(data.data, buffer.data() + offset, data.size);
    offset += data.size;
    return Ok{seri::good};
  }

  std::vector<std::uint8_t> buffer;
  std::size_t offset{};
};

SpiFsDomainStorage::SpiFsDomainStorage() {
  InitFs();
  InitState();
}

SpiFsDomainStorage::~SpiFsDomainStorage() { DeInitFs(); }

std::unique_ptr<IDomainStorageWriter> SpiFsDomainStorage::Store(
    DomainQuery const& query) {
  auto file_path = Format("{}/{}/{}/{}", kBasePath, query.id, query.class_id,
                          static_cast<int>(query.version));
  return std::make_unique<SpiFsSotorageWriter>(*this, file_path, query);
}

ClassList SpiFsDomainStorage::Enumerate(ObjId const& obj_id) {
  auto obj_it = object_map_.find(obj_id);
  if (obj_it == std::end(object_map_)) {
    AE_TELE_INFO(kSpifsDsEnumObjIdNotFound, "Obj not found {}", obj_id);
    return {};
  }

  ClassList classes;
  for (auto const& [class_id, _] : obj_it->second) {
    classes.emplace_back(class_id);
  }
  AE_TELE_DEBUG(kSpifsDsEnumerated, "Enumerated for obj {} classes {}", obj_id,
                classes);
  return classes;
}

DomainLoad SpiFsDomainStorage::Load(DomainQuery const& query) {
  auto obj_map_it = object_map_.find(query.id);
  if (obj_map_it == std::end(object_map_)) {
    AE_TELE_INFO(kSpifsDsLoadObjIdNoFound,
                 "Unable to find object id={}, class id={}, version={}",
                 query.id, query.class_id, static_cast<int>(query.version));
    return {DomainLoadResult::kEmpty, {}};
  }
  if (obj_map_it->second.empty()) {
    return {DomainLoadResult::kRemoved, {}};
  }

  auto class_map_it = obj_map_it->second.find(query.class_id);
  if (class_map_it == std::end(obj_map_it->second)) {
    AE_TELE_INFO(kSpifsDsLoadObjClassIdNotFound,
                 "Unable to find object id={}, class id={}, version={}",
                 query.id, query.class_id, static_cast<int>(query.version));
    return {DomainLoadResult::kEmpty, {}};
  }
  auto version_it = class_map_it->second.find(query.version);
  if (version_it == std::end(class_map_it->second)) {
    AE_TELE_INFO(kSpifsDsLoadObjVersionNotFound,
                 "Unable to find object id={}, class id={}, version={}",
                 query.id, query.class_id, static_cast<int>(query.version));
    return {DomainLoadResult::kEmpty, {}};
  }

  auto file_path = Format("{}/{}/{}/{}", kBasePath, query.id, query.class_id,
                          static_cast<int>(query.version));
  FILE* file = fopen(file_path.c_str(), "r");
  if (file == nullptr) {
    AE_TELED_ERROR("Failed to open file {} for reading.", file_path);
    return {};
  }

  AE_TELE_DEBUG(kSpifsDsObjLoaded,
                "Loaded object id={}, class id={}, version={}", query.id,
                query.class_id, static_cast<int>(query.version));

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
      auto file_path = Format("{}/{}/{}/{}", kBasePath, obj_id, class_id,
                              static_cast<int>(version.first));
      unlink(file_path.c_str());
    }
  }
  obj_map_it->second.clear();
  AE_TELE_DEBUG(kSpifsDsObjRemoved, "Removed object {}", obj_id);
  SyncState();
}

void SpiFsDomainStorage::CleanUp() {
  for (auto const& [obj_id, obj_map_data] : object_map_) {
    for (auto const& [class_id, class_data] : obj_map_data) {
      for (auto version : class_data) {
        auto file_path = Format("{}/{}/{}/{}", kBasePath, obj_id, class_id,
                                static_cast<int>(version.first));
        unlink(file_path.c_str());
      }
    }
  }
  object_map_.clear();
  SyncState();
}

void SpiFsDomainStorage::BeginSaveTransaction() { ++save_tx_depth_; }

void SpiFsDomainStorage::EndSaveTransaction() {
  if (save_tx_depth_ == 0) {
    return;
  }
  --save_tx_depth_;
  if (save_tx_depth_ != 0) {
    return;
  }
  if (map_dirty_) {
    SyncState();
    map_dirty_ = false;
  }
}

void SpiFsDomainStorage::InitFs() {
  esp_vfs_spiffs_conf_t conf = {kBasePath.data(), kPartition.data(), 128, true};

  esp_err_t ret = esp_vfs_spiffs_register(&conf);

  if (ret != ESP_ERR_INVALID_STATE) {
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
  auto* file = fopen(kObjectMapPath.data(), "r");
  if (file == nullptr) {
    AE_TELED_DEBUG("File {} does not exists ", kObjectMapPath);
    return;
  }

  constexpr std::size_t kReadSize = 256;
  std::vector<std::uint8_t> buffer;
  std::size_t off = 0;
  while (true) {
    buffer.resize(off + kReadSize);
    auto res = fread(buffer.data() + off, 1, kReadSize, file);
    off += res;
    if (res < kReadSize) {
      break;
    }
  }
  fclose(file);

  buffer.resize(off);

  auto bin_archive =
      seri::BinaryArchive{seri::BinaryVectorBuffer<std::uint32_t>{buffer}};
  if (auto res = bin_archive.Load(object_map_); !res) {
    AE_TELED_DEBUG("Failed to load object map, error{}", res.error().message);
  }
}

void SpiFsDomainStorage::SyncState() {
  auto const open_t0 = AeExpSaveNowUs();
  auto* file = fopen(kObjectMapPath.data(), "w");
  AeExpSaveAdd(&AeExpSaveSample::map_open_us,
               static_cast<std::uint32_t>(AeExpSaveNowUs() - open_t0));
  if (file == nullptr) {
    AE_TELED_ERROR("Failed to open file {} for writing.", kObjectMapPath);
    AeExpSaveMarkFail();
    return;
  }
  std::vector<std::uint8_t> buffer;
  auto bin_archive =
      seri::BinaryArchive{seri::BinaryVectorBuffer<std::uint32_t>{buffer}};
  auto const seri_t0 = AeExpSaveNowUs();
  if (auto res = bin_archive.Save(object_map_); !res) {
    AeExpSaveAdd(&AeExpSaveSample::map_serialization_us,
                 static_cast<std::uint32_t>(AeExpSaveNowUs() - seri_t0));
    AE_TELED_DEBUG("Failed to save object map, error {}", res.error().message);
    AeExpSaveMarkFail();
  } else {
    AeExpSaveAdd(&AeExpSaveSample::map_serialization_us,
                 static_cast<std::uint32_t>(AeExpSaveNowUs() - seri_t0));
    auto const write_t0 = AeExpSaveNowUs();
    fwrite(buffer.data(), 1, buffer.size(), file);
    AeExpSaveAdd(&AeExpSaveSample::map_fwrite_us,
                 static_cast<std::uint32_t>(AeExpSaveNowUs() - write_t0));
    AeExpSaveInc(&AeExpSaveSample::map_rewrite_count);
    AeExpSaveAdd(&AeExpSaveSample::map_bytes,
                 static_cast<std::uint32_t>(buffer.size()));
    AE_EXP_SAVE("map_rewrite", "path=%s bytes=%zu", kObjectMapPath.data(),
                buffer.size());
  }

  auto const close_t0 = AeExpSaveNowUs();
  fclose(file);
  AeExpSaveAdd(&AeExpSaveSample::map_close_us,
               static_cast<std::uint32_t>(AeExpSaveNowUs() - close_t0));
}

bool SpiFsDomainStorage::ObjectCrcChanged(DomainQuery const& query,
                                          DataCrc crc) const {
  auto obj_it = object_map_.find(query.id);
  if (obj_it == std::end(object_map_)) {
    return true;
  }
  auto class_it = obj_it->second.find(query.class_id);
  if (class_it == std::end(obj_it->second)) {
    return true;
  }
  auto ver_it = class_it->second.find(query.version);
  if (ver_it == std::end(class_it->second)) {
    return true;
  }
  return ver_it->second != crc;
}

void SpiFsDomainStorage::ConfirmObjectWritten(DomainQuery const& query,
                                              DataCrc crc) {
  object_map_[query.id][query.class_id][query.version] = crc;
  map_dirty_ = true;
#if defined(AE_EXP_LEGACY_MAP_SYNC)
  // Baseline: rewrite object_map_dump once per changed object.
  SyncState();
  map_dirty_ = false;
#else
  // Optimized: defer SyncState until outermost EndSaveTransaction.
  if (save_tx_depth_ == 0) {
    SyncState();
    map_dirty_ = false;
  }
#endif
}

}  // namespace ae

#endif  // AE_SPIFS_DOMAIN_STORAGE_ENABLED
