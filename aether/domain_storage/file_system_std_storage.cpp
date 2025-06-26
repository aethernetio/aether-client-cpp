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
  FstreamStorageWriter(DomainQuiery q, std::ofstream&& f)
      : query{std::move(q)}, file{std::move(f)} {}

  ~FstreamStorageWriter() override {
    file.close();
    AE_TELE_DEBUG(
        kFileSystemDsObjSaved, "Saved object id={}, class id={}, version={}",
        query.id.ToString(), query.class_id, static_cast<int>(query.version));
  }

  void write(void const* data, std::size_t size) override {
    file.write(reinterpret_cast<std::ofstream::char_type const*>(data),
               static_cast<std::streamsize>(size));
  }

 private:
  DomainQuiery query;
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

  ReadResult result() const override { return ReadResult::kYes; }
  void result(ReadResult) override {}

 private:
  std::ifstream file;
};

FileSystemStdStorage::FileSystemStdStorage() = default;

FileSystemStdStorage::~FileSystemStdStorage() = default;

std::unique_ptr<IDomainStorageWriter> FileSystemStdStorage::Store(
    DomainQuiery const& query) {
  auto class_dir = std::filesystem::path("state") / query.id.ToString() /
                   std::to_string(query.class_id);

  std::filesystem::create_directories(class_dir);
  auto version_data_path = class_dir / std::to_string(query.version);
  std::ofstream f(version_data_path,
                  std::ios::out | std::ios::binary | std::ios::trunc);

  return std::make_unique<FstreamStorageWriter>(query, std::move(f));
}

ClassList FileSystemStdStorage::Enumerate(const ae::ObjId& obj_id) {
  // collect unique classes
  std::set<uint32_t> classes;

  auto state_dir = std::filesystem::path{"state"};
  auto ec = std::error_code{};
  auto obj_dir = state_dir / obj_id.ToString();
  for (auto const& class_dir :
       std::filesystem::directory_iterator(obj_dir, ec)) {
    auto file_name = class_dir.path().filename().string();
    classes.insert(static_cast<std::uint32_t>(std::stoul(file_name)));
  }
  AE_TELE_DEBUG(kFileSystemDsEnumerated, "Enumerated classes {}", classes);

  if (ec) {
    AE_TELE_ERROR(kFileSystemDsEnumObjIdNotFound,
                  "Unable to open directory with error {}", ec.message());
  }

  return ClassList{classes.begin(), classes.end()};
}

DomainLoad FileSystemStdStorage::Load(DomainQuiery const& query) {
  auto object_dir = std::filesystem::path("state") / query.id.ToString();
  auto ec = std::error_code{};
  if (!std::filesystem::exists(object_dir, ec)) {
    return {DomainLoadResult::kEmpty, {}};
  }

  auto is_dir_empty = [&]() {
    auto iter = std::filesystem::directory_iterator{object_dir};
    return std::filesystem::begin(iter) == std::filesystem::end(iter);
  };
  if (is_dir_empty()) {
    return {DomainLoadResult::kRemoved, {}};
  }

  auto class_dir = object_dir / std::to_string(query.class_id);
  auto version_data_path = class_dir / std::to_string(query.version);
  std::ifstream f(version_data_path, std::ios::in | std::ios::binary);
  if (!f.good()) {
    AE_TELE_ERROR(kFileSystemDsLoadObjClassIdNotFound, "Unable to open file {}",
                  version_data_path.string());
    return DomainLoad{DomainLoadResult::kEmpty, {}};
  }

  AE_TELE_DEBUG(
      kFileSystemDsObjLoaded, "Loaded object id={}, class id={}, version={}",
      query.id.ToString(), query.class_id, static_cast<int>(query.version));

  return {DomainLoadResult::kLoaded,
          std::make_unique<FstreamStorageReader>(std::move(f))};
}

void FileSystemStdStorage::Remove(ae::ObjId const& obj_id) {
  auto object_dir = std::filesystem::path("state") / obj_id.ToString();
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

  for (auto const& class_dir :
       std::filesystem::directory_iterator(object_dir, ec)) {
    auto ec2 = std::error_code{};
    std::filesystem::remove_all(class_dir.path(), ec2);
    if (ec2) {
      AE_TELED_ERROR("Unable to remove dir {}, error {}",
                     class_dir.path().string(), ec2.message());
      continue;
    }
    AE_TELE_DEBUG(kFileSystemDsObjRemoved, "Object removed {}",
                  obj_id.ToString());
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
