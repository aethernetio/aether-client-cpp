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
    AE_TELE_DEBUG(DsObjSaved, "Saved object id={}, class id={}, version={}",
                  query.id.ToString(), query.class_id,
                  static_cast<int>(query.version));
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
  auto obj_dir = std::filesystem::path("state") /
                 std::to_string(query.version) / query.id.ToString();
  std::filesystem::create_directories(obj_dir);
  auto class_path = obj_dir / std::to_string(query.class_id);
  std::ofstream f(class_path,
                  std::ios::out | std::ios::binary | std::ios::trunc);

  return std::make_unique<FstreamStorageWriter>(query, std::move(f));
}

ClassList FileSystemStdStorage::Enumerate(const ae::ObjId& obj_id) {
  // collect unique classes
  std::set<uint32_t> classes;

  auto state_dir = std::filesystem::path{"state"};
  auto ec = std::error_code{};
  for (auto const& version_dir :
       std::filesystem::directory_iterator(state_dir, ec)) {
    auto obj_dir = version_dir.path() / obj_id.ToString();

    auto ec2 = std::error_code{};
    for (auto const& f : std::filesystem::directory_iterator(obj_dir, ec2)) {
      auto file_name = f.path().filename().string();
      classes.insert(static_cast<std::uint32_t>(std::stoul(file_name)));
    }
    if (ec2) {
      AE_TELED_ERROR("Unable to open directory with error {}", ec2.message());
    }
  }
  AE_TELE_DEBUG(DsEnumerated, "Enumerated classes {}", classes);

  if (ec) {
    AE_TELE_ERROR(DsEnumObjIdNotFound, "Unable to open directory with error {}",
                  ec.message());
  }

  return ClassList{classes.begin(), classes.end()};
}

DomainLoad FileSystemStdStorage::Load(DomainQuiery const& query) {
  auto obj_dir = std::filesystem::path("state") /
                 std::to_string(query.version) / query.id.ToString();
  auto class_path = obj_dir / std::to_string(query.class_id);
  std::ifstream f(class_path, std::ios::in | std::ios::binary);
  if (!f.good()) {
    AE_TELE_ERROR(DsLoadObjClassIdNotFound, "Unable to open file {}",
                  class_path.string());
    return DomainLoad{DomainLoadResult::kEmpty, {}};
  }

  char is_removed{};
  f >> is_removed;
  if (is_removed == '~') {
    return {DomainLoadResult::kRemoved, {}};
  }
  // go back to the beginning
  f.seekg(std::ios::beg);

  AE_TELE_DEBUG(DsObjLoaded, "Loaded object id={}, class id={}, version={}",
                query.id.ToString(), query.class_id,
                static_cast<int>(query.version));

  return {DomainLoadResult::kLoaded,
          std::make_unique<FstreamStorageReader>(std::move(f))};
}

void FileSystemStdStorage::Remove(const ae::ObjId& obj_id) {
  std::filesystem::path state_dir = std::filesystem::path{"state"};

  auto ec = std::error_code{};
  for (auto const& version_dir :
       std::filesystem::directory_iterator(state_dir, ec)) {
    auto obj_dir = version_dir.path() / obj_id.ToString();
    auto ec2 = std::error_code{};
    for (auto const& f : std::filesystem::directory_iterator(obj_dir, ec2)) {
      auto class_file = std::ofstream{
          f.path(), std::ios::out | std::ios::binary | std::ios::trunc};
      // replace file data with ~ - means data was removed
      class_file.write("~", 1);
    }
    AE_TELE_DEBUG(DsObjRemoved, "Removed object {} of version {}",
                  obj_id.ToString(), version_dir.path().filename());
  }
  if (ec) {
    AE_TELE_ERROR(DsRemoveObjError, "Unable to open directory with error {}",
                  ec.message());
  }
}

void FileSystemStdStorage::CleanUp() {
  std::filesystem::remove_all("state");
  AE_TELED_DEBUG("Removed all!", 0);
}
}  // namespace ae

#endif  // AE_FILE_SYSTEM_STD_ENABLED
