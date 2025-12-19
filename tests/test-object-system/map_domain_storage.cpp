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

#include "tests/test-object-system/map_domain_storage.h"

#include "aether/mstream_buffers.h"

namespace ae {
class MapDomainStorageWriter final : public IDomainStorageWriter {
 public:
  MapDomainStorageWriter(DataKey key, std::uint8_t version, MapDomainStorage& s)
      : key{key}, version{version}, storage{&s}, vector_writer{data_buffer} {}
  ~MapDomainStorageWriter() override {
    storage->SaveData(key, version, std::move(data_buffer));
  }

  void write(void const* data, std::size_t size) override {
    vector_writer.write(data, size);
  }

  DataKey key;
  std::uint8_t version;
  MapDomainStorage* storage;
  DataValue data_buffer;
  VectorWriter<IDomainStorageWriter::size_type> vector_writer;
};

class MapDomainStorageReader final : public IDomainStorageReader {
 public:
  MapDomainStorageReader(DataValue const& d, MapDomainStorage& s)
      : storage{&s}, data_buffer{&d}, reader{*data_buffer} {}

  void read(void* data, std::size_t size) override { reader.read(data, size); }

  MapDomainStorage* storage;
  DataValue const* data_buffer;
  VectorReader<IDomainStorageReader::size_type> reader;
};

std::unique_ptr<IDomainStorageWriter> MapDomainStorage::Store(
    DataKey key, std::uint8_t version) {
  return std::make_unique<MapDomainStorageWriter>(key, version, *this);
}

DomainLoad MapDomainStorage::Load(DataKey key, std::uint8_t version) {
  auto obj_map_it = map.find(key);
  if (obj_map_it == std::end(map)) {
    return {DomainLoadResult::kEmpty, {}};
  }
  if (!obj_map_it->second) {
    return {DomainLoadResult::kRemoved, {}};
  }

  auto version_it = obj_map_it->second->find(version);
  if (version_it == std::end(*obj_map_it->second)) {
    return {DomainLoadResult::kEmpty, {}};
  }

  return {DomainLoadResult::kLoaded,
          std::make_unique<MapDomainStorageReader>(version_it->second, *this)};
}

void MapDomainStorage::Remove(DataKey key) {
  auto obj_map_it = map.find(key);
  if (obj_map_it == std::end(map)) {
    // if there was no object, explicitly mark it as removed
    map.emplace(key, std::nullopt);
    return;
  }

  obj_map_it->second.reset();
}

void MapDomainStorage::CleanUp() { map.clear(); }

void MapDomainStorage::SaveData(DataKey key, std::uint8_t version,
                                DataValue&& data) {
  auto& versioned_data = map[key];
  if (!versioned_data) {
    versioned_data.emplace();
  }
  auto& saved = (*versioned_data)[version];
  saved = std::move(data);
}

}  // namespace ae
