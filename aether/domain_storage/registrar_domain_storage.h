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

#ifndef AETHER_DOMAIN_STORAGE_REGISTRAR_DOMAIN_STORAGE_H_
#define AETHER_DOMAIN_STORAGE_REGISTRAR_DOMAIN_STORAGE_H_

#define REGISTRAR_DOMAIN_STORAGE_ENABLED 1

#include <filesystem>

#include "aether/obj/idomain_storage.h"
#include "aether/domain_storage/ram_domain_storage.h"

namespace ae {
class RegistrarDomainStorage : public IDomainStorage {
 public:
  explicit RegistrarDomainStorage(std::filesystem::path file_path);
  ~RegistrarDomainStorage() override;

  std::unique_ptr<IDomainStorageWriter> Store(
      DomainQuiery const& query) override;
  ClassList Enumerate(ObjId const& obj_id) override;
  DomainLoad Load(DomainQuiery const& query) override;
  void Remove(const ae::ObjId& obj_id) override;
  void CleanUp() override;

 private:
  void SaveState();
  void PrintData(std::ofstream& file, std::vector<std::uint8_t> const& data);
  template <typename K, typename T>
  void PrintMapKeysAsData(std::ofstream& file, std::map<K, T> const& map);

  std::filesystem::path file_path_;
  RamDomainStorage ram_storage;
};
}  // namespace ae

#endif  // AETHER_DOMAIN_STORAGE_REGISTRAR_DOMAIN_STORAGE_H_
