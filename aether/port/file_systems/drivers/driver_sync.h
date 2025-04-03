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

#ifndef AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SYNC_H_
#define AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SYNC_H_

namespace ae {

enum class DriverFsType : std::uint8_t {
  kDriverHeader,
  kDriverStd,
  kDriverRam,
  kDriverSpifs,
  kDriverNone
};

class DriverSync {
 public:
  DriverSync(enum class DriverFsType fs_driver_type);
  ~DriverSync();
  void DriverSyncRead(const std::string &path,
                        std::vector<std::uint8_t> &data_vector);
  void DriverSyncWrite(const std::string &path,
                         const std::vector<std::uint8_t> &data_vector);
  void DriverSyncDelete(const std::string &path);
 private:
  enum class DriverFsType fs_driver_type_{kDriverNone};
}

}  // namespace ae

#endif  // AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_SYNC_H_