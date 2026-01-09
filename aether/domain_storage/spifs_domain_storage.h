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

#ifndef AETHER_DOMAIN_STORAGE_SPIFS_DOMAIN_STORAGE_H_
#define AETHER_DOMAIN_STORAGE_SPIFS_DOMAIN_STORAGE_H_

#include "aether/config.h"

#if defined ESP_PLATFORM && AE_SUPPORT_SPIFS_FS
#  define AE_SPIFS_DOMAIN_STORAGE_ENABLED 1

#  include <map>
#  include <vector>
#  include <string_view>

#  include "aether/obj/idomain_storage.h"

namespace ae {
class SpiFsDomainStorage : public IDomainStorage {
  static constexpr std::string_view kPartition = "storage";
  static constexpr std::string_view kBasePath = "/spiffs";
  static constexpr std::string_view kObjectMapPath = "/spiffs/object_map_dump";

  using VersionList = std::vector<std::uint8_t>;
  using ClassMap = std::map<std::uint32_t, VersionList>;
  using ObjectMap = std::map<ObjId, ClassMap>;

 public:
  SpiFsDomainStorage();
  ~SpiFsDomainStorage() override;

  std::unique_ptr<IDomainStorageWriter> Store(
      DomainQuery const& query) override;
  ClassList Enumerate(ObjId const& obj_id) override;
  DomainLoad Load(DomainQuery const& query) override;
  void Remove(const ae::ObjId& obj_id) override;
  void CleanUp() override;

 private:
  void InitFs();
  void DeInitFs();
  void InitState();
  void SyncState();

  bool SpifsRead(std::string_view path, ObjectData& out);
  bool SpifsWrite(std::string_view path, ObjectData const& in);
  bool FileRemove(std::string_view path);

  ObjectMap object_map_;
};
}  // namespace ae

#endif
#endif  // AETHER_DOMAIN_STORAGE_SPIFS_DOMAIN_STORAGE_H_
