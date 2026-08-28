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

#ifndef TESTS_TEST_DOMAIN_STORAGE_CRC_MAP_COMMIT_STORAGE_H_
#define TESTS_TEST_DOMAIN_STORAGE_CRC_MAP_COMMIT_STORAGE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "aether/obj/idomain_storage.h"

namespace ae {

/**
 * \brief In-memory DomainStorage double that mirrors SpiFsDomainStorage
 * Save-transaction commit semantics (CRC skip, map_dirty, nested Begin/End).
 *
 * Object data and the serialized CRC map are held in a shared
 * \ref CommittedState so a second instance can Load from the same committed
 * state (round-trip / new "Domain" reader path).
 */
class CrcMapCommitStorage : public IDomainStorage {
  friend class CrcMapCommitStorageWriter;

 public:
  using DataCrc = std::uint32_t;
  using VersionMap = std::map<std::uint8_t, DataCrc>;
  using ClassMap = std::map<std::uint32_t, VersionMap>;
  using ObjectMap = std::map<ObjId, ClassMap>;

  using VersionData = std::map<std::uint8_t, ObjectData>;
  using ClassData = std::map<std::uint32_t, VersionData>;
  using ObjectFiles = std::map<ObjId, ClassData>;

  struct CommittedState {
    ObjectFiles files;
    ObjectData map_blob;
  };

  explicit CrcMapCommitStorage(
      std::shared_ptr<CommittedState> committed = nullptr);
  ~CrcMapCommitStorage() override;

  std::unique_ptr<IDomainStorageWriter> Store(
      DomainQuery const& query) override;
  ClassList Enumerate(ObjId const& obj_id) override;
  DomainLoad Load(DomainQuery const& query) override;
  void Remove(ObjId const& obj_id) override;
  void CleanUp() override;
  void BeginSaveTransaction() override;
  void EndSaveTransaction() override;

  void ResetCounters();

  std::shared_ptr<CommittedState> const& committed() const {
    return committed_;
  }

  // Public counters for tests (SpiFs AeExpSaveSample analogues).
  std::uint32_t object_write_count{0};
  std::uint32_t map_rewrite_count{0};
  std::uint32_t crc_skip_count{0};
  std::uint32_t map_bytes{0};

  // When set, writers fail after CRC-changed check and do not Confirm.
  bool inject_write_failure{false};

 private:
  void InitState();
  void SyncState();

  bool ObjectCrcChanged(DomainQuery const& query, DataCrc crc) const;
  void ConfirmObjectWritten(DomainQuery const& query, DataCrc crc);

  std::shared_ptr<CommittedState> committed_;
  ObjectMap object_map_;
  std::uint32_t save_tx_depth_{0};
  bool map_dirty_{false};
};

}  // namespace ae

#endif  // TESTS_TEST_DOMAIN_STORAGE_CRC_MAP_COMMIT_STORAGE_H_
