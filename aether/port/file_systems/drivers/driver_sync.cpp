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
#include "aether/port/file_systems/drivers/driver_functions.h"
#include "aether/tele/tele.h"

namespace ae {

DriverSync::DriverSync(std::unique_ptr<DriverHeader> fs_driver_source,
                       std::unique_ptr<DriverBase> fs_driver_destination) {
  fs_driver_source_ = std::move(fs_driver_source);
  fs_driver_destination_ = std::move(fs_driver_destination);
}

DriverSync::~DriverSync() {}

void DriverSync::DriverRead(const PathStructure &path,
                            std::vector<std::uint8_t> &data_vector) {
#if defined(AE_DISTILLATION)
  fs_driver_destination_->DriverRead(path, data_vector, false);
#else
  DriverSyncronize_(path);
  if (fs_driver_destination_ != nullptr) {
    fs_driver_destination_->DriverRead(path, data_vector, false);
  }
#endif
}

void DriverSync::DriverWrite(const PathStructure &path,
                             const std::vector<std::uint8_t> &data_vector) {
#if defined(AE_DISTILLATION)
  fs_driver_destination_->DriverWrite(path, data_vector);
#else
  DriverSyncronize_(path);
  if (fs_driver_destination_ != nullptr) {
    fs_driver_destination_->DriverWrite(path, data_vector);
  }
#endif
}

void DriverSync::DriverDelete(const PathStructure &path) {
#if defined(AE_DISTILLATION)
  fs_driver_destination_->DriverDelete(path);
#else
  DriverSyncronize_(path);
  if (fs_driver_destination_ != nullptr) {
    fs_driver_destination_->DriverDelete(path);
  }
#endif
}

std::vector<PathStructure> DriverSync::DriverDir(const PathStructure &path) {
  std::vector<std::string> dirs_list_source{};
  std::vector<PathStructure> dirs_list_destination{};
  std::vector<std::string> dirs_list_destination_str{};
  std::vector<PathStructure> dirs_list_result{};
  std::vector<std::string> dirs_list_result_str{};

#if !defined(AE_DISTILLATION)
  dirs_list_destination = fs_driver_destination_->DriverDir(path);
#else
  if (fs_driver_source_ != nullptr) {
    dirs_list_source = fs_driver_source_->DriverDir(GetPathString(path));
  }
  if (fs_driver_destination_ != nullptr) {
    dirs_list_destination = fs_driver_destination_->DriverDir(path);
    for (dir : dirs_list_destination) {
      dirs_list_destination_str.push_back(GetPathString(dir));
    }
  }
#endif

  dirs_list_result_str =
      CombineIgnoreDuplicates(dirs_list_source, dirs_list_destination_str);

  for (auto dir : dirs_list_result_str) {
    dirs_list_result.push_back(GetPathStructure(dir));
  }

  return dirs_list_result;
}

void DriverSync::DriverSyncronize_(const PathStructure &path) {
  std::vector<std::uint8_t> data_vector_source;
  std::vector<std::uint8_t> data_vector_destination;

  if (fs_driver_source_->GetDriverFsType() == DriverFsType::kDriverNone) {
    return;
  }

  if (fs_driver_source_->GetDriverFsType() == DriverFsType::kDriverHeader) {
    fs_driver_source_->DriverRead(GetPathString(path), data_vector_source,
                                  true);
  } else {
    fs_driver_source_->DriverRead(GetPathString(path), data_vector_source,
                                  false);
  }
  if (fs_driver_destination_ != nullptr) {
    fs_driver_destination_->DriverRead(path, data_vector_destination, false);

    if (data_vector_destination.size() == 0) {
      if (!IsEqual(data_vector_source, data_vector_destination)) {
        fs_driver_destination_->DriverDelete(path);
        fs_driver_destination_->DriverWrite(path, data_vector_source);
      }
    }
  }
}

}  // namespace ae
