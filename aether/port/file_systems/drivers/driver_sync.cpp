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
  bool res{false};
  if (v1.size() == 0 || v2.size() == 0) {
    return res;
  }
  if (std::equal(v1.begin(), v1.end(), v2.begin())) {
    res = true;
  }

  return res;
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
  fs_driver_source_ = DriverFactory::Create(fs_driver_type_);
#if defined(ESP_PLATFORM)
  fs_driver_destination_ = DriverFactory::Create(DriverFsType::kDriverSpifs);
#else
  fs_driver_destination_ = DriverFactory::Create(DriverFsType::kDriverStd);
#endif
}

DriverSync::~DriverSync() {}

void DriverSync::DriverRead(const std::string &path,
                            std::vector<std::uint8_t> &data_vector) {
#if defined(AE_DISTILLATION)
  if (fs_driver_type_ == DriverFsType::kDriverHeader) {
    fs_driver_source_->DriverRead(path, data_vector, true);
  } else {
    fs_driver_source_->DriverRead(path, data_vector, false);
  }
#else
  DriverSyncronize_(path);
  fs_driver_destination_->DriverRead(path, data_vector, false);
#endif
}

void DriverSync::DriverWrite(const std::string &path,
                             const std::vector<std::uint8_t> &data_vector) {
#if defined(AE_DISTILLATION)
  fs_driver_source_->DriverWrite(path, data_vector);
#else
  DriverSyncronize_(path);
  fs_driver_destination_->DriverWrite(path, data_vector);
#endif
}

void DriverSync::DriverDelete(const std::string &path) {
#if defined(AE_DISTILLATION)
  fs_driver_source_->DriverDelete(path);
#else
  DriverSyncronize_(path);
  fs_driver_destination_->DriverDelete(path);
#endif
}

std::vector<std::string> DriverSync::DriverDir(const std::string &path) {
  std::vector<std::string> dirs_list_source{};
  std::vector<std::string> dirs_list_destination{};
  std::vector<std::string> dirs_list_result{};

#if defined(AE_DISTILLATION)
  dirs_list_source = fs_driver_source_->DriverDir(path);
#else
  dirs_list_source = fs_driver_source_->DriverDir(path);
  dirs_list_destination = fs_driver_destination_->DriverDir(path);
#endif

  dirs_list_result =
      CombineIgnoreDuplicates(dirs_list_source, dirs_list_destination);

  return dirs_list_result;
}

void DriverSync::DriverSyncronize_(const std::string &path) {
  std::vector<std::uint8_t> data_vector_source;
  std::vector<std::uint8_t> data_vector_destination;

  if (fs_driver_type_ == DriverFsType::kDriverHeader) {
    fs_driver_source_->DriverRead(path, data_vector_source, true);
  } else {
    fs_driver_source_->DriverRead(path, data_vector_source, false);
  }
  fs_driver_destination_->DriverRead(path, data_vector_destination, false);

  if (data_vector_destination.size() == 0) {
    if (!IsEqual(data_vector_source, data_vector_destination)) {
      fs_driver_destination_->DriverDelete(path);
      fs_driver_destination_->DriverWrite(path, data_vector_source);
    }
  }
}

}  // namespace ae
