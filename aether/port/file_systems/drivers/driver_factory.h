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

#ifndef AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_FACTORY_H_
#define AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_FACTORY_H_

#include "aether/port/file_systems/drivers/driver_base.h"

namespace ae {

enum class DriverFsType : std::uint8_t {
  kDriverNone,
  kDriverHeader,
  kDriverStd,
  kDriverRam,
  kDriverSpifsV1,
  kDriverSpifsV2
};

class DriverFactory {
 public:
  static std::unique_ptr<DriverBase> Create(enum DriverFsType fs_driver_type);
};
}  // namespace ae

#endif  // AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_FACTORY_H_
