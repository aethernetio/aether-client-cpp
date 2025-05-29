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
  MapDomainStorageWriter(DomainQuiery q, MapDomainStorage& s)
      : query{std::move(q)}, storage{&s}, vector_writer{data} {}
  ~MapDomainStorageWriter() override {
    storage->SaveData(query, std::move(data));
  }

  void write(void const* data, std::size_t size) override {
    vector_writer.write(data, size);
  }

  DomainQuiery query;
  MapDomainStorage* storage;
  ObjectData data;
  VectorWriter<IDomainStorageWriter::size_type> vector_writer;
};

class MapDomainStorageReader final : public IDomainStorageReader {
 public:
  explicit MapDomainStorageReader(ObjectData const& d)
      : data{&d}, reader{*data} {}

  void read(void* data, std::size_t size) override { reader.read(data, size); }

  ReadResult result() const override { return ReadResult::kYes; }
  void result(ReadResult) override {}

  ObjectData const* data;
  VectorReader<IDomainStorageReader::size_type> reader;
};

std::unique_ptr<IDomainStorageWriter> MapDomainStorage::Store(
    DomainQuiery const& query) {
  return std::make_unique<MapDomainStorageWriter>(query, *this);
}

ClassList MapDomainStorage::Enumerate(ObjId const& obj_id) {
  auto class_data_it = map.find(obj_id.id());
  if (class_data_it == std::end(map)) {
    return {};
  }
  ClassList classes;
  classes.reserve(class_data_it->second.size());
  for (auto& [cls, _] : class_data_it->second) {
    classes.push_back(cls);
  }
  return classes;
}

DomainLoad MapDomainStorage::Load(DomainQuiery const& query) {
  auto obj_map_it = map.find(query.id.id());
  if (obj_map_it == std::end(map)) {
    return DomainLoad{DomainLoadResult::kEmpty, {}};
  }
  auto class_map_it = obj_map_it->second.find(query.class_id);
  if (class_map_it == std::end(obj_map_it->second)) {
    return DomainLoad{DomainLoadResult::kEmpty, {}};
  }
  auto version_it = class_map_it->second.find(query.version);
  if (version_it == std::end(class_map_it->second)) {
    return DomainLoad{DomainLoadResult::kEmpty, {}};
  }
  if (!version_it->second) {
    return DomainLoad{DomainLoadResult::kRemoved, {}};
  }
  return DomainLoad{
      DomainLoadResult::kLoaded,
      std::make_unique<MapDomainStorageReader>(*version_it->second)};
}

void MapDomainStorage::Remove(ObjId const& obj_id) {
  auto obj_map_it = map.find(obj_id.id());
  if (obj_map_it == std::end(map)) {
    return;
  }
  for (auto& [_, class_data] : obj_map_it->second) {
    for (auto& [_, version_data] : class_data) {
      version_data.reset();
    }
  }
}

void MapDomainStorage::CleanUp() { map.clear(); }

void MapDomainStorage::SaveData(DomainQuiery const& query, ObjectData&& data) {
  map[query.id.id()][query.class_id][query.version] = std::move(data);
}

}  // namespace ae
