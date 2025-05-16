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

#include "aether/port/file_systems/drivers/driver_functions.h"
#include "aether/port/file_systems/file_systems_tele.h"
namespace ae {

inline bool IsInteger(const std::string& s) {
  if (s.empty() || ((!isdigit(s[0])) && (s[0] != '-') && (s[0] != '+')))
    return false;

  char* p;
  std::strtol(s.c_str(), &p, 10);

  return (*p == 0);
}

ae::PathStructure GetPathStructure(const std::string& path) {
  ae::PathStructure path_struct{};

  // Path is "state/version/obj_id/class_id"
  auto pos1 = path.find("/");
  if (pos1 != std::string::npos) {
    auto pos2 = path.find("/", pos1 + 1);
    if (pos2 != std::string::npos) {
      auto pos3 = path.find("/", pos2 + 1);

      if (IsInteger(path.substr(pos1 + 1, pos2 - pos1 - 1))) {
        path_struct.version = static_cast<std::uint8_t>(
            std::stoul(path.substr(pos1 + 1, pos2 - pos1 - 1)));
      }
      if (IsInteger(path.substr(pos2 + 1, pos3 - pos2 - 1))) {
        path_struct.obj_id = static_cast<std::uint32_t>(
            std::stoul(path.substr(pos2 + 1, pos3 - pos2 - 1)));
      }
      if (IsInteger(path.substr(pos3 + 1, path.length() - pos3 - 1))) {
        path_struct.class_id = static_cast<std::uint32_t>(
            std::stoul(path.substr(pos3 + 1, path.length() - pos3 - 1)));
      }
    }
  }

  return path_struct;
}

std::string GetPathString(const ae::PathStructure& path, bool convert) {
  std::string path_string{};

  if(convert){
  #if (defined(_WIN64) || defined(_WIN32))
    path_string = "state\\" + std::to_string(path.version) + "\\" +
                  path.obj_id.ToString() + "\\" + std::to_string(path.class_id);
  #else
    path_string = "state/" + std::to_string(path.version) + "/" +
                  path.obj_id.ToString() + "/" + std::to_string(path.class_id);
  #endif
  } else {
    path_string = std::to_string(path.version) + "/" +
                  path.obj_id.ToString() + "/" + std::to_string(path.class_id);    
  }

  return path_string;
}

}  // namespace ae
