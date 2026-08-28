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

#include "crc_map_commit_storage.h"

#include <cstring>
#include <utility>

#include "aether-miscpp/crc.h"
#include "aether-miscpp/serialization/binary_archive.h"

namespace ae {
namespace crc_map_commit_storage_internal {

class CrcMapCommitStorageReader final : public IDomainStorageReader {
 public:
  explicit CrcMapCommitStorageReader(ObjectData data)
      : buffer_{std::move(data)} {}

  seri::SeriResult Read(seri::SizeReadTag data) override {
    std::uint32_t size{};
    if (auto res = Read(seri::DataTag{size}); !res) {
      return res;
    }
    data.size = static_cast<std::size_t>(size);
    return Ok{seri::good};
  }

  seri::SeriResult Read(seri::DataReadTag data) override {
    if ((offset_ + data.size) > buffer_.size()) {
      return Error{seri::read_eof};
    }
    std::memcpy(data.data, buffer_.data() + offset_, data.size);
    offset_ += data.size;
    return Ok{seri::good};
  }

 private:
  ObjectData buffer_;
  std::size_t offset_{0};
};

}  // namespace crc_map_commit_storage_internal

class CrcMapCommitStorageWriter final : public IDomainStorageWriter {
 public:
  CrcMapCommitStorageWriter(CrcMapCommitStorage& storage, DomainQuery query)
      : storage_{&storage}, query_{std::move(query)} {}

  ~CrcMapCommitStorageWriter() override {
    auto const crc = crc32::from_buffer(buffer_.data(), buffer_.size()).value;

    if (!storage_->ObjectCrcChanged(query_, crc)) {
      ++storage_->crc_skip_count;
      return;
    }

    if (storage_->inject_write_failure) {
      return;
    }

    storage_->committed_->files[query_.id][query_.class_id][query_.version] =
        buffer_;
    ++storage_->object_write_count;
    storage_->ConfirmObjectWritten(query_, crc);
  }

  seri::SeriResult Write(seri::SizeWriteTag data) override {
    auto const size = static_cast<std::uint32_t>(data.size);
    return Write(seri::DataTag{size});
  }

  seri::SeriResult Write(seri::DataWriteTag data) override {
    auto const* p = static_cast<std::uint8_t const*>(data.data);
    buffer_.insert(std::end(buffer_), p, p + data.size);
    return Ok{seri::good};
  }

 private:
  CrcMapCommitStorage* storage_;
  DomainQuery query_;
  ObjectData buffer_;
};

CrcMapCommitStorage::CrcMapCommitStorage(
    std::shared_ptr<CommittedState> committed)
    : committed_{committed ? std::move(committed)
                           : std::make_shared<CommittedState>()} {
  InitState();
}

CrcMapCommitStorage::~CrcMapCommitStorage() = default;

void CrcMapCommitStorage::ResetCounters() {
  object_write_count = 0;
  map_rewrite_count = 0;
  crc_skip_count = 0;
  map_bytes = 0;
}

std::unique_ptr<IDomainStorageWriter> CrcMapCommitStorage::Store(
    DomainQuery const& query) {
  return std::make_unique<CrcMapCommitStorageWriter>(*this, query);
}

ClassList CrcMapCommitStorage::Enumerate(ObjId const& obj_id) {
  auto obj_it = object_map_.find(obj_id);
  if (obj_it == std::end(object_map_)) {
    return {};
  }

  ClassList classes;
  for (auto const& [class_id, _] : obj_it->second) {
    classes.emplace_back(class_id);
  }
  return classes;
}

DomainLoad CrcMapCommitStorage::Load(DomainQuery const& query) {
  auto obj_map_it = object_map_.find(query.id);
  if (obj_map_it == std::end(object_map_)) {
    return {DomainLoadResult::kEmpty, {}};
  }
  if (obj_map_it->second.empty()) {
    return {DomainLoadResult::kRemoved, {}};
  }

  auto class_map_it = obj_map_it->second.find(query.class_id);
  if (class_map_it == std::end(obj_map_it->second)) {
    return {DomainLoadResult::kEmpty, {}};
  }
  auto version_it = class_map_it->second.find(query.version);
  if (version_it == std::end(class_map_it->second)) {
    return {DomainLoadResult::kEmpty, {}};
  }

  auto files_obj_it = committed_->files.find(query.id);
  if (files_obj_it == std::end(committed_->files)) {
    return {DomainLoadResult::kEmpty, {}};
  }
  auto files_class_it = files_obj_it->second.find(query.class_id);
  if (files_class_it == std::end(files_obj_it->second)) {
    return {DomainLoadResult::kEmpty, {}};
  }
  auto files_ver_it = files_class_it->second.find(query.version);
  if (files_ver_it == std::end(files_class_it->second)) {
    return {DomainLoadResult::kEmpty, {}};
  }

  return {DomainLoadResult::kLoaded,
          std::make_unique<crc_map_commit_storage_internal::
                               CrcMapCommitStorageReader>(files_ver_it->second)};
}

void CrcMapCommitStorage::Remove(ObjId const& obj_id) {
  auto obj_map_it = object_map_.find(obj_id);
  if (obj_map_it == std::end(object_map_)) {
    object_map_.emplace(obj_id, ClassMap{});
    SyncState();
    return;
  }

  committed_->files.erase(obj_id);
  obj_map_it->second.clear();
  SyncState();
}

void CrcMapCommitStorage::CleanUp() {
  committed_->files.clear();
  object_map_.clear();
  SyncState();
}

void CrcMapCommitStorage::BeginSaveTransaction() { ++save_tx_depth_; }

void CrcMapCommitStorage::EndSaveTransaction() {
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

void CrcMapCommitStorage::InitState() {
  if (committed_->map_blob.empty()) {
    return;
  }

  auto bin_archive = seri::BinaryArchive{
      seri::BinaryVectorBuffer<std::uint32_t>{committed_->map_blob}};
  (void)bin_archive.Load(object_map_);
}

void CrcMapCommitStorage::SyncState() {
  ObjectData buffer;
  auto bin_archive =
      seri::BinaryArchive{seri::BinaryVectorBuffer<std::uint32_t>{buffer}};
  if (auto res = bin_archive.Save(object_map_); !res) {
    return;
  }

  committed_->map_blob = buffer;
  ++map_rewrite_count;
  map_bytes = static_cast<std::uint32_t>(buffer.size());
}

bool CrcMapCommitStorage::ObjectCrcChanged(DomainQuery const& query,
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

void CrcMapCommitStorage::ConfirmObjectWritten(DomainQuery const& query,
                                              DataCrc crc) {
  object_map_[query.id][query.class_id][query.version] = crc;
  map_dirty_ = true;
  // Outside a SaveRoot transaction, flush immediately (SpiFs default /
  // non-transactional callers). Optional AE_EXP_LEGACY_MAP_SYNC contrast is
  // not required for these tests.
  if (save_tx_depth_ == 0) {
    SyncState();
    map_dirty_ = false;
  }
}

}  // namespace ae
