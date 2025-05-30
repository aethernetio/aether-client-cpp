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

#ifndef AETHER_DOMAIN_STORAGE_STATIC_DOMAIN_STORAGE_H_
#define AETHER_DOMAIN_STORAGE_STATIC_DOMAIN_STORAGE_H_

#if defined FS_INIT
#  define STATIC_DOMAIN_STORAGE_ENABLED 1

#  include "aether/obj/idomain_storage.h"

namespace ae {
class StaticDomainStorage final : public IDomainStorage {
 public:
  StaticDomainStorage();
  ~StaticDomainStorage() override;

  std::unique_ptr<IDomainStorageWriter> Store(
      DomainQuiery const& query) override;
  ClassList Enumerate(ObjId const& obj_id) override;
  DomainLoad Load(DomainQuiery const& query) override;
  void Remove(ObjId const& obj_id) override;
  void CleanUp() override;
};
}  // namespace ae

#endif
#endif  // AETHER_DOMAIN_STORAGE_STATIC_DOMAIN_STORAGE_H_
