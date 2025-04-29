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

#include <memory>

#include "aether/port/file_systems/drivers/driver_factory.h"

#include "aether/port/file_systems/drivers/driver_sync.h"
#include "aether/port/file_systems/drivers/driver_header.h"
#include "aether/port/file_systems/drivers/driver_ram.h"
#include "aether/port/file_systems/drivers/driver_spifs_v1.h"
#include "aether/port/file_systems/drivers/driver_spifs_v2.h"
#include "aether/port/file_systems/drivers/driver_std.h"

namespace ae {
std::unique_ptr<DriverBase> DriverFactory::Create(
    enum DriverFsType fs_driver_type) {
  std::unique_ptr<DriverBase> ret{std::make_unique<DriverRam>()};
  switch (fs_driver_type) {
    case DriverFsType::kDriverStd:
#if defined(AE_FILE_SYSTEM_STD_ENABLED)
      ret = std::make_unique<DriverStd>();
#endif  // defined(AE_FILE_SYSTEM_STD_ENABLED)
      break;
    case DriverFsType::kDriverRam:
      ret = std::make_unique<DriverRam>();
      break;
    case DriverFsType::kDriverHeader:
      ret = std::make_unique<DriverHeader>();
      break;
    case DriverFsType::kDriverSpifsV1:
#if defined(ESP_PLATFORM)
      ret = std::make_unique<DriverSpifsV1>();
#endif  // (defined(ESP_PLATFORM))
      break;
    case DriverFsType::kDriverSpifsV2:
#if defined(ESP_PLATFORM)
      ret = std::make_unique<DriverSpifsV2>();
#endif  // (defined(ESP_PLATFORM))
      break;
    case DriverFsType::kDriverNone:
      ret = nullptr;
      break;
    default:
      assert(0);
      break;
  }

  return ret;
}

}  // namespace ae
