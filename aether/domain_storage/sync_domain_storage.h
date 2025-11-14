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

#ifndef AETHER_DOMAIN_STORAGE_SYNC_DOMAIN_STORAGE_H_
#define AETHER_DOMAIN_STORAGE_SYNC_DOMAIN_STORAGE_H_

#include <memory>

#include "aether/obj/idomain_storage.h"

namespace ae {
class SyncDomainStorage final : public IDomainStorage {
 public:
  SyncDomainStorage(std::unique_ptr<IDomainStorage> read_only,
                    std::unique_ptr<IDomainStorage> read_write);

  std::unique_ptr<IDomainStorageWriter> Store(
      DomainQuery const& query) override;
  ClassList Enumerate(ObjId const& obj_id) override;
  DomainLoad Load(DomainQuery const& query) override;
  void Remove(ObjId const& obj_id) override;
  void CleanUp() override;

 private:
  std::unique_ptr<IDomainStorage> read_only_;
  std::unique_ptr<IDomainStorage> read_write_;
};
}  // namespace ae

#endif  // AETHER_DOMAIN_STORAGE_SYNC_DOMAIN_STORAGE_H_
