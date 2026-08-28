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

#ifndef AETHER_OBJ_IDOMAIN_STORAGE_H_
#define AETHER_OBJ_IDOMAIN_STORAGE_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"

#include "aether/obj/obj_id.h"

namespace ae {

enum class DomainLoadResult : std::uint8_t {
  kEmpty,    //< There is not data for queury
  kRemoved,  //< Data for query was removed
  kLoaded,   //< Data loaded successfully
};

using ObjectData = std::vector<std::uint8_t>;
using ClassList = std::vector<std::uint32_t>;

struct DomainQuery {
  ObjId id;
  std::uint32_t class_id;
  std::uint8_t version;
};

// BinaryBuffer implementation for storage writer
class IDomainStorageWriter {
 public:
  virtual ~IDomainStorageWriter() = default;

  virtual seri::SeriResult Write(seri::SizeWriteTag data) = 0;
  virtual seri::SeriResult Write(seri::DataWriteTag data) = 0;
};

// BinaryBuffer implementation for storage reader
class IDomainStorageReader {
 public:
  virtual ~IDomainStorageReader() = default;

  virtual seri::SeriResult Read(seri::SizeReadTag data) = 0;
  virtual seri::SeriResult Read(seri::DataReadTag data) = 0;
};

struct DomainLoad {
  DomainLoadResult result;
  std::unique_ptr<IDomainStorageReader> reader;
};

/**
 * \brief IDomainFacility to store object's data.
 */
class IDomainStorage {
 public:
  virtual ~IDomainStorage() = default;
  /**
   * \brief Store an ObjectData by query.
   */
  virtual std::unique_ptr<IDomainStorageWriter> Store(
      DomainQuery const& query) = 0;
  /**
   * \brief Enumerate all classes for object.
   */
  virtual ClassList Enumerate(ObjId const& obj_id) = 0;
  /**
   * \brief Load object data by query.
   */
  virtual DomainLoad Load(DomainQuery const& query) = 0;

  /**
   * \brief Remove object data by query
   */
  virtual void Remove(ObjId const& obj_id) = 0;
  /**
   * \brief Clean up the whole storage.
   */
  virtual void CleanUp() = 0;

  /**
   * \brief Begin / end an outermost DomainGraph::SaveRoot transaction.
   * Nested SaveRoot calls share the same transaction. Default: no-op.
   */
  virtual void BeginSaveTransaction() {}
  virtual void EndSaveTransaction() {}
};
}  // namespace ae

#endif  // AETHER_OBJ_IDOMAIN_STORAGE_H_
