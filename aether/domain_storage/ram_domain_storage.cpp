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
  RamDomainStorageWriter(DomainQuiery q, RamDomainStorage& s)
      : query{std::move(q)}, storage{&s}, vector_writer{data_buffer} {
    assert(!storage->write_lock);
  }
  ~RamDomainStorageWriter() override {
    storage->SaveData(query, std::move(data_buffer));
  }

  void write(void const* data, std::size_t size) override {
    vector_writer.write(data, size);
  }

  DomainQuiery query;
  RamDomainStorage* storage;
  ObjectData data_buffer;
  VectorWriter<IDomainStorageWriter::size_type> vector_writer;
};

class RamDomainStorageReader final : public IDomainStorageReader {
 public:
  RamDomainStorageReader(ObjectData const& d, RamDomainStorage& s)
      : storage{&s}, data_buffer{&d}, reader{*data_buffer} {
    storage->write_lock = true;
  }
  ~RamDomainStorageReader() override { storage->write_lock = false; }

  void read(void* data, std::size_t size) override { reader.read(data, size); }

  ReadResult result() const override { return ReadResult::kYes; }
  void result(ReadResult) override {}

  RamDomainStorage* storage;
  ObjectData const* data_buffer;
  VectorReader<IDomainStorageReader::size_type> reader;
};

RamDomainStorage::RamDomainStorage() = default;

RamDomainStorage::~RamDomainStorage() = default;

std::unique_ptr<IDomainStorageWriter> RamDomainStorage::Store(
    DomainQuiery const& query) {
  return std::make_unique<RamDomainStorageWriter>(query, *this);
}

ClassList RamDomainStorage::Enumerate(ObjId const& obj_id) {
  auto obj_map_it = state.find(obj_id);
  if (obj_map_it == std::end(state)) {
    AE_TELE_ERROR(kRamDsEnumObjIdNotFound, "Obj not found {}",
                  obj_id.ToString());
    return {};
  }
  if (!obj_map_it->second) {
    return {};
  }

  ClassList classes;
  classes.reserve(obj_map_it->second->size());
  for (auto& [cls, _] : *obj_map_it->second) {
    classes.emplace_back(cls);
  }
  AE_TELE_DEBUG(kRamDsEnumerated, "Enumerated for obj {} classes {}",
                obj_id.ToString(), classes);
  return classes;
}

DomainLoad RamDomainStorage::Load(DomainQuiery const& query) {
  auto obj_map_it = state.find(query.id);
  if (obj_map_it == std::end(state)) {
    AE_TELE_ERROR(kRamDsLoadObjIdNoFound,
                  "Unable to find object id={}, class id={}, version={}",
                  query.id.ToString(), query.class_id,
                  static_cast<int>(query.version));
    return {DomainLoadResult::kEmpty, {}};
  }
  if (!obj_map_it->second) {
    return {DomainLoadResult::kRemoved, {}};
  }

  auto class_map_it = obj_map_it->second->find(query.class_id);
  if (class_map_it == std::end(*obj_map_it->second)) {
    AE_TELE_ERROR(kRamDsLoadObjClassIdNotFound,
                  "Unable to find object id={}, class id={}, version={}",
                  query.id.ToString(), query.class_id,
                  static_cast<int>(query.version));
    return {DomainLoadResult::kEmpty, {}};
  }
  auto version_it = class_map_it->second.find(query.version);
  if (version_it == std::end(class_map_it->second)) {
    AE_TELE_ERROR(kRamDsLoadObjVersionNotFound,
                  "Unable to find object id={}, class id={}, version={}",
                  query.id.ToString(), query.class_id,
                  static_cast<int>(query.version));
    return {DomainLoadResult::kEmpty, {}};
  }

  AE_TELE_DEBUG(kRamDsObjLoaded,
                "Loaded object id={}, class id={}, version={}, size={}",
                query.id.ToString(), query.class_id,
                static_cast<int>(query.version), version_it->second.size());

  return {DomainLoadResult::kLoaded,
          std::make_unique<RamDomainStorageReader>(version_it->second, *this)};
}

void RamDomainStorage::Remove(ObjId const& obj_id) {
  auto obj_map_it = state.find(obj_id);
  if (obj_map_it == std::end(state)) {
    state.emplace(obj_id, std::nullopt);
    return;
  }

  obj_map_it->second.reset();
  AE_TELE_DEBUG(kRamDsObjRemoved, "Removed object {}", obj_id.ToString());
}

void RamDomainStorage::CleanUp() { state.clear(); }

void RamDomainStorage::SaveData(DomainQuiery const& query, ObjectData&& data) {
  auto& objcect_classes = state[query.id];
  if (!objcect_classes) {
    objcect_classes.emplace();
  }
  auto& saved = (*objcect_classes)[query.class_id][query.version];
  saved = std::move(data);
  AE_TELE_DEBUG(kRamDsObjSaved,
                "Saved object id={}, class id={}, version={}, size={}",
                query.id.ToString(), query.class_id,
                std::to_string(query.version), saved.size());
}

}  // namespace ae

#endif  // AE_FILE_SYSTEM_RAM_ENABLED
