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

#include "aether/domain_storage/sync_domain_storage.h"

namespace ae {
SyncDomainStorage::SyncDomainStorage(std::unique_ptr<IDomainStorage> read_only,
                                     std::unique_ptr<IDomainStorage> read_write)
    : read_only_{std::move(read_only)}, read_write_{std::move(read_write)} {}

std::unique_ptr<IDomainStorageWriter> SyncDomainStorage::Store(
    DataKey key, std::uint8_t version) {
  return read_write_->Store(key, version);
}

DomainLoad SyncDomainStorage::Load(DataKey key, std::uint8_t version) {
  auto rw_load = read_write_->Load(key, version);
  if (rw_load.result == DomainLoadResult::kEmpty) {
    return read_only_->Load(key, version);
  }
  return rw_load;
}

void SyncDomainStorage::Remove(DataKey key) { read_write_->Remove(key); }

void SyncDomainStorage::CleanUp() { read_write_->CleanUp(); }

}  // namespace ae
