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

#ifndef AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEMS_TELE_H_
#define AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEMS_TELE_H_

#include "aether/tele/tele.h"

AE_TELE_MODULE(kFileSystems, 10);

AE_TAG_INDEXED(FsEnumerated, kFileSystems, 300)
AE_TAG(FsObjSaved, kFileSystems)
AE_TAG(FsObjLoaded, kFileSystems)
AE_TAG(FsLoadObjVersionNotFound, kFileSystems)
AE_TAG(FsLoadObjIdNoFound, kFileSystems)
AE_TAG(FsLoadObjClassIdNotFound, kFileSystems)
AE_TAG(FsEnumObjIdNotFound, kFileSystems)
AE_TAG(FsRemoveObjIdNoFound, kFileSystems)
AE_TAG(FsObjRemoved, kFileSystems)

#endif  // AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEMS_TELE_H_
