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
#include "aether/port/file_systems/drivers/driver_spifs.h"
#include "aether/port/file_systems/drivers/driver_std.h"

namespace ae {
std::unique_ptr<DriverBase> DriverFactory::Create(enum DriverFsType fs_driver_type) {
  switch (fs_driver_type) {
    case DriverFsType::kDriverStd:
      return std::make_unique<DriverStd>();
      break;
    case DriverFsType::kDriverRam:
      return std::make_unique<DriverRam>();
      break;
    case DriverFsType::kDriverSpifs:
#if (defined(ESP_PLATFORM))
      return std::make_unique<DriverSpifs>();
#endif  // (defined(ESP_PLATFORM))
      break;
    default:
      assert(0);
      break;
  }
  
  return std::make_unique<DriverStd>();
}

}  // namespace ae
