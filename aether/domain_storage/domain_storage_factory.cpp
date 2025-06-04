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

#include "aether/domain_storage/domain_storage_factory.h"

// IWYU pragma: begin_keeps
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/domain_storage/sync_domain_storage.h"
#include "aether/domain_storage/spifs_domain_storage.h"
#include "aether/domain_storage/static_domain_storage.h"
#include "aether/domain_storage/file_system_std_storage.h"

#if defined FS_INIT
#  define STATIC_DOMAIN_STORAGE_ENABLED 1
#  include FS_INIT
#endif

// IWYU pragma: end_keeps

namespace ae {
std::unique_ptr<IDomainStorage> DomainStorageFactory::Create() {
#if defined STATIC_DOMAIN_STORAGE_ENABLED
  auto ro_storage = make_unique<StaticDomainStorage>(static_domain_data);
  auto rw_storage = CreateRwStorage();
  return make_unique<SyncDomainStorage>(std::move(ro_storage),
                                        std::move(rw_storage));
#else
  return CreateRwStorage();
#endif
}

std::unique_ptr<IDomainStorage> DomainStorageFactory::CreateRwStorage() {
#if defined AE_FILE_SYSTEM_STD_ENABLED
  return make_unique<FileSystemStdStorage>();
#elif defined AE_SPIFS_DOMAIN_STORAGE_ENABLED
  return make_unique<SpiFsDomainStorage>();
#elif defined AE_FILE_SYSTEM_RAM_ENABLED
  return make_unique<RamDomainStorage>();
#endif
}

}  // namespace ae
