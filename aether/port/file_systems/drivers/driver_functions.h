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

#ifndef AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_FUNCTIONS_H_
#define AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_FUNCTIONS_H_

#include <unordered_set>

#include "aether/obj/domain.h"
#include "aether/port/file_systems/drivers/driver_base.h"

namespace ae {

ae::PathStructure GetPathStructure(const std::string &path);
std::string GetPathString(const ae::PathStructure& path, bool convert);

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

}  // namespace ae

#endif  // AETHER_PORT_FILE_SYSTEMS_DRIVERS_DRIVER_FUNCTIONS_H_
