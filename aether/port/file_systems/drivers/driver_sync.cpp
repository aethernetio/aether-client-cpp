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

#include "aether/port/file_systems/drivers/driver_sync.h"
#include "aether/port/file_systems/drivers/driver_factory.h"
#include "aether/tele/tele.h"

namespace ae {

template <typename T>
bool IsEqual(std::vector<T> const &v1, std::vector<T> const &v2) {
  auto pair = std::mismatch(v1.begin(), v1.end(), v2.begin());
  return (pair.first == v1.end() && pair.second == v2.end());
}

template <typename T>
std::vector<T> CombineIgnoreDuplicates(const std::vector<T> &a,
                                         const std::vector<T> &b) {
  std::unordered_set<T> unique_elements(a.begin(), a.end());
  std::vector<T> result = a;
  for (const auto &elem : b) {
    if (unique_elements.find(elem) == unique_elements.end()) {
      result.push_back(elem);
    }
  }
  return result;
}

DriverSync::DriverSync(enum DriverFsType fs_driver_type) {
  fs_driver_type_ = fs_driver_type;
  DriverSource = DriverFactory::Create(fs_driver_type_);
#if defined(ESP_PLATFORM)
  DriverDestination = DriverFactory::Create(DriverFsType::kDriverSpifs);
#else
  DriverDestination = DriverFactory::Create(DriverFsType::kDriverStd);
#endif
}

DriverSync::~DriverSync() {}

void DriverSync::DriverRead(const std::string &path,
                            std::vector<std::uint8_t> &data_vector) {
#if defined(AE_DISTILLATION)
  DriverSource->DriverRead(path, data_vector);
#else
  DriverSyncronize_(std::move(DriverSource), std::move(DriverDestination),
                    path);
  DriverDestination->DriverRead(path, data_vector);
#endif
}

void DriverSync::DriverWrite(const std::string &path,
                             const std::vector<std::uint8_t> &data_vector) {
#if defined(AE_DISTILLATION)
  DriverSource->DriverWrite(path, data_vector);
#else
  DriverSyncronize_(std::move(DriverSource), std::move(DriverDestination),
                    path);
  DriverDestination->DriverWrite(path, data_vector);
#endif
}

void DriverSync::DriverDelete(const std::string &path) {
#if defined(AE_DISTILLATION)
  DriverSource->DriverDelete(path);
#else
  DriverSyncronize_(std::move(DriverSource), std::move(DriverDestination),
                    path);
  DriverDestination->DriverDelete(path);
#endif
}

std::vector<std::string> DriverSync::DriverDir(const std::string &path) {
  std::vector<std::string> dirs_list_source{};
  std::vector<std::string> dirs_list_destination{};
  std::vector<std::string> dirs_list_result{};

#if defined(AE_DISTILLATION)
  dirs_list_source = DriverSource->DriverDir(path);
#else
  dirs_list_source = DriverSource->DriverDir(path);
  dirs_list_destination = DriverDestination->DriverDir(path);
#endif

  dirs_list_result =
      CombineIgnoreDuplicates(dirs_list_source, dirs_list_destination);

  return dirs_list_result;
}

void DriverSync::DriverSyncronize_(
    std::unique_ptr<DriverBase> DrvSource,
    std::unique_ptr<DriverBase> DrvDestination, const std::string &path) {
  std::vector<std::uint8_t> data_vector_source;
  std::vector<std::uint8_t> data_vector_destination;

  DrvSource->DriverRead(path, data_vector_source);
  DrvDestination->DriverRead(path, data_vector_destination);

  if (!IsEqual(data_vector_source, data_vector_destination)) {
    DrvDestination->DriverDelete(path);
    DrvDestination->DriverWrite(path, data_vector_source);
  }
}

}  // namespace ae
