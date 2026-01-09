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

#include "aether/domain_storage/file_system_std_storage.h"

#if defined AE_FILE_SYSTEM_STD_ENABLED

#  include <ios>
#  include <set>
#  include <string>
#  include <fstream>
#  include <filesystem>
#  include <system_error>

#  include "aether/domain_storage/domain_storage_tele.h"

namespace ae {

class FstreamStorageWriter final : public IDomainStorageWriter {
 public:
  FstreamStorageWriter(DataKey key, std::uint8_t version, std::ofstream&& f)
      : key{key}, version{version}, file{std::move(f)} {}

  ~FstreamStorageWriter() override {
    file.close();
    AE_TELE_DEBUG(kFileSystemDsObjSaved, "Saved data key={}, version={}", key,
                  static_cast<int>(version));
  }

  void write(void const* data, std::size_t size) override {
    file.write(reinterpret_cast<std::ofstream::char_type const*>(data),
               static_cast<std::streamsize>(size));
  }

 private:
  DataKey key;
  std::uint8_t version;
  std::ofstream file;
};

class FstreamStorageReader final : public IDomainStorageReader {
 public:
  explicit FstreamStorageReader(std::ifstream&& f) : file{std::move(f)} {}
  ~FstreamStorageReader() override { file.close(); }

  void read(void* data, std::size_t size) override {
    file.read(reinterpret_cast<std::ofstream::char_type*>(data),
              static_cast<std::streamsize>(size));
  }

 private:
  std::ifstream file;
};

FileSystemStdStorage::FileSystemStdStorage() = default;

FileSystemStdStorage::~FileSystemStdStorage() = default;

std::unique_ptr<IDomainStorageWriter> FileSystemStdStorage::Store(
    DataKey key, std::uint8_t version) {
  auto data_dir = std::filesystem::path("state") / std::to_string(key);

  std::filesystem::create_directories(data_dir);
  auto version_data_path = data_dir / std::to_string(version);
  std::ofstream f(version_data_path,
                  std::ios::out | std::ios::binary | std::ios::trunc);

  return std::make_unique<FstreamStorageWriter>(key, version, std::move(f));
}

DomainLoad FileSystemStdStorage::Load(DataKey key, std::uint8_t version) {
  auto data_dir = std::filesystem::path("state") / std::to_string(key);
  auto ec = std::error_code{};
  if (!std::filesystem::exists(data_dir, ec)) {
    AE_TELE_INFO(kFileSystemDsLoadDataKeyNotFound,
                 "Data storage for key={}, version={} not found", key,
                 static_cast<int>(version));
    return {DomainLoadResult::kEmpty, {}};
  }

  auto is_dir_empty = [&]() {
    auto iter = std::filesystem::directory_iterator{data_dir};
    return std::filesystem::begin(iter) == std::filesystem::end(iter);
  };
  if (is_dir_empty()) {
    return {DomainLoadResult::kRemoved, {}};
  }

  auto version_data_path = data_dir / std::to_string(version);
  std::ifstream f(version_data_path, std::ios::in | std::ios::binary);
  if (!f.good()) {
    AE_TELE_ERROR(kFileSystemDsEnumOpenFileFailed, "Unable to open file {}",
                  version_data_path.string());
    return DomainLoad{DomainLoadResult::kEmpty, {}};
  }

  AE_TELE_DEBUG(kFileSystemDsObjLoaded, "Loaded object key={}, version={}", key,
                static_cast<int>(version));

  return {DomainLoadResult::kLoaded,
          std::make_unique<FstreamStorageReader>(std::move(f))};
}

void FileSystemStdStorage::Remove(DataKey key) {
  auto object_dir = std::filesystem::path("state") / std::to_string(key);
  auto ec = std::error_code{};
  if (!std::filesystem::exists(object_dir, ec)) {
    std::filesystem::create_directory(object_dir);
    return;
  }
  if (ec) {
    AE_TELED_ERROR("Unable to check if dir exists {} error {}",
                   object_dir.string(), ec.message());
    return;
  }

  for (auto const& version_data_path :
       std::filesystem::directory_iterator(object_dir, ec)) {
    auto ec2 = std::error_code{};
    std::filesystem::remove_all(version_data_path.path(), ec2);
    if (ec2) {
      AE_TELED_ERROR("Unable to remove dir {}, error {}",
                     version_data_path.path().string(), ec2.message());
      continue;
    }
    AE_TELE_DEBUG(kFileSystemDsObjRemoved, "Object removed {}",
                  std::to_string(key));
  }
  if (ec) {
    AE_TELE_ERROR(kFileSystemDsRemoveObjError,
                  "Unable to open directory with error {}", ec.message());
  }
}

void FileSystemStdStorage::CleanUp() {
  std::filesystem::remove_all("state");
  AE_TELED_DEBUG("Removed all!", 0);
}
}  // namespace ae

#endif  // AE_FILE_SYSTEM_STD_ENABLED
