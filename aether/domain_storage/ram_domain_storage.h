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

#ifndef AETHER_DOMAIN_STORAGE_RAM_DOMAIN_STORAGE_H_
#define AETHER_DOMAIN_STORAGE_RAM_DOMAIN_STORAGE_H_

#define AE_FILE_SYSTEM_RAM_ENABLED 1

#include <map>
#include <cstdint>
#include <optional>

#include "aether/obj/idomain_storage.h"

namespace ae {
class RamDomainStorage : public IDomainStorage {
 public:
  using VersionData = std::map<std::uint8_t, DataValue>;
  using ObjClassData = std::map<DataKey, std::optional<VersionData>>;

  RamDomainStorage();
  ~RamDomainStorage() override;

  std::unique_ptr<IDomainStorageWriter> Store(DataKey key,
                                              std::uint8_t version) override;
  DomainLoad Load(DataKey key, std::uint8_t version) override;
  void Remove(DataKey key) override;
  void CleanUp() override;

  void SaveData(DataKey key, std::uint8_t version, DataValue&& data);

  ObjClassData state;
  bool write_lock = false;
};
}  // namespace ae
#endif  // AETHER_DOMAIN_STORAGE_RAM_DOMAIN_STORAGE_H_ */
