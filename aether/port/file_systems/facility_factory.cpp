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

#include "aether/port/file_systems/facility_factory.h"

#include "aether/port/file_systems/file_system_std.h"
#include "aether/port/file_systems/file_system_ram.h"
#include "aether/port/file_systems/file_system_spifs_v2.h"
#include "aether/port/file_systems/file_system_spifs_v1.h"
#include "aether/port/file_systems/file_system_header.h"

namespace ae {
Ptr<IDomainFacility> DomainFacilityFactory::Create() {
#if defined AE_FILE_SYSTEM_STD_ENABLED
  return MakePtr<FileSystemStdFacility>();
#elif defined AE_FILE_SYSTEM_SPIFS_V2_ENABLED
  return MakePtr<FileSystemSpiFsV2Facility>();
#elif defined AE_FILE_SYSTEM_SPIFS_V1_ENABLED
  return MakePtr<FileSystemSpiFsV1Facility>();
#elif defined AE_FILE_SYSTEM_RAM_ENABLED
  return MakePtr<FileSystemRamFacility>();
#endif
}
}  // namespace ae
