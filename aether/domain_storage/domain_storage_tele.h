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

AE_TELE_MODULE(kDomainStorage, 10);

AE_TAG_INDEXED(DsEnumerated, kDomainStorage, 300)
AE_TAG(DsObjSaved, kDomainStorage)
AE_TAG(DsObjLoaded, kDomainStorage)
AE_TAG(DsLoadObjVersionNotFound, kDomainStorage)
AE_TAG(DsLoadObjIdNoFound, kDomainStorage)
AE_TAG(DsLoadObjClassIdNotFound, kDomainStorage)
AE_TAG(DsEnumObjIdNotFound, kDomainStorage)
AE_TAG(DsRemoveObjError, kDomainStorage)
AE_TAG(DsRemoveObjIdNoFound, kDomainStorage)
AE_TAG(DsObjRemoved, kDomainStorage)
AE_TAG(DsStorageInit, kDomainStorage)
AE_TAG(DsStorageMountError, kDomainStorage)
AE_TAG(DsStorageInitError, kDomainStorage)

#endif  // AETHER_DOMAIN_STORAGE_DOMAIN_STORAGE_TELE_H_
