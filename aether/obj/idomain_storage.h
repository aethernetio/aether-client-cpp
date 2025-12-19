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

#include <vector>
#include <memory>
#include <cstdint>

#include "aether/misc/hash.h"  // IWYU pragma: keep

namespace ae {

enum class DomainLoadResult : std::uint8_t {
  kEmpty,    //< There is not data for queury
  kRemoved,  //< Data for query was removed
  kLoaded,   //< Data loaded successfully
};

using DataKey = std::uint32_t;
using DataValue = std::vector<std::uint8_t>;

class IDomainStorageWriter {
 public:
  using size_type = std::uint32_t;

  virtual ~IDomainStorageWriter() = default;

  virtual void write(void const* data, std::size_t size) = 0;
};

class IDomainStorageReader {
 public:
  using size_type = std::uint32_t;

  virtual ~IDomainStorageReader() = default;

  virtual void read(void* data, std::size_t size) = 0;
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
   * \brief Store an DataValue by query.
   */
  virtual std::unique_ptr<IDomainStorageWriter> Store(DataKey key,
                                                      std::uint8_t version) = 0;
  /**
   * \brief Load object data by query.
   */
  virtual DomainLoad Load(DataKey key, std::uint8_t version) = 0;
  /**
   * \brief Remove object data by query
   */
  virtual void Remove(DataKey key) = 0;
  /**
   * \brief Clean up the whole storage.
   */
  virtual void CleanUp() = 0;
};
}  // namespace ae

#endif  // AETHER_OBJ_IDOMAIN_STORAGE_H_
