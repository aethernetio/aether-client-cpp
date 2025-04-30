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

#include <string>
#if (!defined(ESP_PLATFORM))
#  include <filesystem>
#endif

#include "aether/port/file_systems/drivers/driver_header.h"
#include "aether/port/file_systems/drivers/driver_functions.h"
#include "aether/transport/low_level/tcp/data_packet_collector.h"
#include "aether/tele/tele.h"

constexpr char string1[] = "#ifndef CONFIG_FILE_SYSTEM_INIT_H_";
constexpr char string2[] = "#define CONFIG_FILE_SYSTEM_INIT_H_";
constexpr char string3[] = "";
constexpr char string4[] = "#include <array>";
constexpr char string5[] = "";
constexpr char string6a[] = "constexpr std::array<std::uint8_t, ";
constexpr char string6b[] = "> init_fs{";
constexpr char string7[] = "};";
constexpr char string8[] = "";
constexpr char string9[] = "#endif /* CONFIG_FILE_SYSTEM_INIT_H_ */";
constexpr char string10[] = "";

namespace ae {
DriverHeader::DriverHeader() {}

DriverHeader::~DriverHeader() {}

void DriverHeader::DriverRead(const std::string &path,
                              std::vector<std::uint8_t> &data_vector,
                              bool sync) {
  if (sync == false) {
#if (!defined(ESP_PLATFORM))
    std::string line{};

    std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);

    if (!file.good()) {
      AE_TELED_ERROR("Unable to open file {}", path);
      return;
    }

    std::getline(file, line);
    if (line.find(string1) == std::string::npos) {
      return;
    }
    std::getline(file, line);
    if (line.find(string2) == std::string::npos) {
      return;
    }
    std::getline(file, line);
    if (line.find(string3) == std::string::npos) {
      return;
    }
    std::getline(file, line);
    if (line.find(string4) == std::string::npos) {
      return;
    }
    std::getline(file, line);
    if (line.find(string5) == std::string::npos) {
      return;
    }

    std::getline(file, line);

    while (std::getline(file, line)) {
      if (line.find(string7) != std::string::npos) break;
      auto string_len = (line.length() / 6) * 6;
      for (unsigned int i = 0; i < string_len; i += 6) {
        std::string byteString = line.substr(i + 2, 2);
        std::uint8_t byte = HexToByte_(byteString);
        data_vector.push_back(byte);
      }
    }

    AE_TELED_DEBUG("Loaded header file {}", path);
#endif
  } else {
    auto data_vector_ =
        std::vector<std::uint8_t>{init_fs.begin(), init_fs.end()};

    VectorReader<PacketSize> vr{data_vector_};
    ae::PathStructure path_struct{};
    ObjClassData obj_data;

    auto is = imstream{vr};
    // add obj data
    is >> obj_data;

    path_struct = GetPathStructure(path);

    auto obj_it = obj_data.find(path_struct.obj_id);
    if (obj_it == obj_data.end()) {
      return;
    }

    auto class_it = obj_it->second.find(path_struct.class_id);
    if (class_it == obj_it->second.end()) {
      return;
    }

    auto version_it = class_it->second.find(path_struct.version);
    if (version_it == class_it->second.end()) {
      return;
    }

    AE_TELED_DEBUG("Object id={} & class id = {} version {} loaded!",
                   path_struct.obj_id.ToString(), path_struct.class_id,
                   std::to_string(path_struct.version));
    data_vector = version_it->second;

    AE_TELED_DEBUG("Loaded state/{}/{}/{} size: {}",
                   std::to_string(path_struct.version),
                   path_struct.obj_id.ToString(), path_struct.class_id,
                   data_vector.size());
  }
}

void DriverHeader::DriverWrite(const std::string &path,
                               const std::vector<std::uint8_t> &data_vector) {
#if (!defined(ESP_PLATFORM))
  size_t last_index = path.find_last_of("/");
  std::string raw_path = path.substr(0, last_index);

  std::filesystem::create_directories(raw_path);

  uint8_t cnt{0};

  std::ofstream file(path.c_str(),
                     std::ios::out | std::ios::binary | std::ios::trunc);

  file << string1 << std::endl;
  file << string2 << std::endl;
  file << string3 << std::endl;
  file << string4 << std::endl;
  file << string5 << std::endl;
  file << string6a << data_vector.size() << string6b << std::endl;

  for (std::uint8_t byte : data_vector) {
    auto str = ByteToHex_(byte);
    file << str << ", ";
    if (++cnt == 12) {
      cnt = 0;
      file << std::endl;
    }
  }
  if (cnt != 0) {
    file << std::endl;
  }

  file << string7 << std::endl;
  file << string8 << std::endl;
  file << string9 << std::endl;
  file << string10 << std::endl;

  AE_TELED_DEBUG("Saved header file {}", path);
#endif
}

void DriverHeader::DriverDelete(const std::string &path) {
#if (!defined(ESP_PLATFORM))
  std::filesystem::remove(path);
  AE_TELED_DEBUG("Removed header file {}", path);
#endif
}

std::vector<std::string> DriverHeader::DriverDir(const std::string &path) {
  std::vector<std::string> dirs_list{};

  AE_TELED_DEBUG("Dir header file {}", path);
  return dirs_list;
}

std::string DriverHeader::ByteToHex_(std::uint8_t ch) {
  std::stringstream ss;
  ss << "0x" << std::hex << std::setw(2) << std::setfill('0')
     << static_cast<int>(ch);
  return ss.str();
}

uint8_t DriverHeader::HexToByte_(const std::string &hex) {
  return static_cast<uint8_t>(std::stoul(hex, nullptr, 16));
}

}  // namespace ae
