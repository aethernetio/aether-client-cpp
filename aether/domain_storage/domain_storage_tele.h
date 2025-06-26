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

#ifndef AETHER_DOMAIN_STORAGE_DOMAIN_STORAGE_TELE_H_
#define AETHER_DOMAIN_STORAGE_DOMAIN_STORAGE_TELE_H_

#include "aether/tele/tele.h"

AE_TELE_MODULE(kDomainStorage, 9, 171, 185);
AE_TELE_MODULE(kDomainStorageDebug, 109, 300, 320);

AE_TAG(kFileSystemDsLoadObjClassIdNotFound, kDomainStorage)
AE_TAG(kFileSystemDsEnumObjIdNotFound, kDomainStorage)
AE_TAG(kFileSystemDsRemoveObjError, kDomainStorage)

AE_TAG(kFileSystemDsEnumerated, kDomainStorageDebug)
AE_TAG(kFileSystemDsObjSaved, kDomainStorageDebug)
AE_TAG(kFileSystemDsObjLoaded, kDomainStorageDebug)
AE_TAG(kFileSystemDsObjRemoved, kDomainStorageDebug)

AE_TAG(kRamDsLoadObjVersionNotFound, kDomainStorage)
AE_TAG(kRamDsLoadObjIdNoFound, kDomainStorage)
AE_TAG(kRamDsLoadObjClassIdNotFound, kDomainStorage)
AE_TAG(kRamDsEnumObjIdNotFound, kDomainStorage)
AE_TAG(kRamDsRemoveObjError, kDomainStorage)

AE_TAG(kRamDsEnumerated, kDomainStorageDebug)
AE_TAG(kRamDsObjSaved, kDomainStorageDebug)
AE_TAG(kRamDsObjLoaded, kDomainStorageDebug)
AE_TAG(kRamDsObjRemoved, kDomainStorageDebug)

AE_TAG(kSpifsDsLoadObjVersionNotFound, kDomainStorage)
AE_TAG(kSpifsDsLoadObjIdNoFound, kDomainStorage)
AE_TAG(kSpifsDsLoadObjClassIdNotFound, kDomainStorage)
AE_TAG(kSpifsDsEnumObjIdNotFound, kDomainStorage)
AE_TAG(kSpifsDsStorageInit, kDomainStorage)
AE_TAG(kSpifsDsStorageMountError, kDomainStorage)
AE_TAG(kSpifsDsStorageInitError, kDomainStorage)

AE_TAG(kSpifsDsEnumerated, kDomainStorageDebug)
AE_TAG(kSpifsDsObjSaved, kDomainStorageDebug)
AE_TAG(kSpifsDsObjLoaded, kDomainStorageDebug)
AE_TAG(kSpifsDsObjRemoved, kDomainStorageDebug)

#endif  // AETHER_DOMAIN_STORAGE_DOMAIN_STORAGE_TELE_H_
