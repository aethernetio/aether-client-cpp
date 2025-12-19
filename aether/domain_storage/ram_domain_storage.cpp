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

#include "aether/domain_storage/ram_domain_storage.h"

#if defined AE_FILE_SYSTEM_RAM_ENABLED

#  include <cassert>

#  include "aether/mstream_buffers.h"
#  include "aether/domain_storage/domain_storage_tele.h"

namespace ae {
class RamDomainStorageWriter final : public IDomainStorageWriter {
 public:
  RamDomainStorageWriter(DataKey key, std::uint8_t version, RamDomainStorage& s)
      : key{key}, version{version}, storage{&s}, vector_writer{data_buffer} {
    assert(!storage->write_lock);
  }
  ~RamDomainStorageWriter() override {
    storage->SaveData(key, version, std::move(data_buffer));
  }

  void write(void const* data, std::size_t size) override {
    vector_writer.write(data, size);
  }

  DataKey key;
  std::uint8_t version;
  RamDomainStorage* storage;
  DataValue data_buffer;
  VectorWriter<IDomainStorageWriter::size_type> vector_writer;
};

class RamDomainStorageReader final : public IDomainStorageReader {
 public:
  RamDomainStorageReader(DataValue const& d, RamDomainStorage& s)
      : storage{&s}, data_buffer{&d}, reader{*data_buffer} {
    storage->write_lock = true;
  }
  ~RamDomainStorageReader() override { storage->write_lock = false; }

  void read(void* data, std::size_t size) override { reader.read(data, size); }

  RamDomainStorage* storage;
  DataValue const* data_buffer;
  VectorReader<IDomainStorageReader::size_type> reader;
};

RamDomainStorage::RamDomainStorage() = default;

RamDomainStorage::~RamDomainStorage() = default;

std::unique_ptr<IDomainStorageWriter> RamDomainStorage::Store(
    DataKey key, std::uint8_t version) {
  return std::make_unique<RamDomainStorageWriter>(key, version, *this);
}

DomainLoad RamDomainStorage::Load(DataKey key, std::uint8_t version) {
  auto obj_map_it = state.find(key);
  if (obj_map_it == std::end(state)) {
    AE_TELE_ERROR(kRamDsLoadObjIdNoFound,
                  "Unable to find data key={}, version={}", key,
                  static_cast<int>(version));
    return {DomainLoadResult::kEmpty, {}};
  }
  if (!obj_map_it->second) {
    return {DomainLoadResult::kRemoved, {}};
  }

  auto version_it = obj_map_it->second->find(version);
  if (version_it == std::end(*obj_map_it->second)) {
    AE_TELE_ERROR(kRamDsLoadObjVersionNotFound,
                  "Unable to find data key={key}, version={}", key,
                  static_cast<int>(version));
    return {DomainLoadResult::kEmpty, {}};
  }

  AE_TELE_DEBUG(kRamDsObjLoaded, "Loaded data key={}, version={}, size={}", key,
                static_cast<int>(version), version_it->second.size());

  return {DomainLoadResult::kLoaded,
          std::make_unique<RamDomainStorageReader>(version_it->second, *this)};
}

void RamDomainStorage::Remove(DataKey key) {
  auto obj_map_it = state.find(key);
  if (obj_map_it == std::end(state)) {
    // if there was no object, explicitly mark it as removed
    state.emplace(key, std::nullopt);
    return;
  }

  obj_map_it->second.reset();
  AE_TELE_DEBUG(kRamDsObjRemoved, "Removed data {}", key);
}

void RamDomainStorage::CleanUp() { state.clear(); }

void RamDomainStorage::SaveData(DataKey key, std::uint8_t version,
                                DataValue&& data) {
  auto& versioned_data = state[key];
  if (!versioned_data) {
    versioned_data.emplace();
  }
  auto& saved = (*versioned_data)[version];
  saved = std::move(data);
  AE_TELE_DEBUG(kRamDsObjSaved, "Saved data key={}, version={}, size={}", key,
                static_cast<int>(version), saved.size());
}

}  // namespace ae

#endif  // AE_FILE_SYSTEM_RAM_ENABLED
