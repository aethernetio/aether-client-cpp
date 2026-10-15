/*
 * Copyright 2026 Aethernet Inc.
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

#include "stream_matrix/bench_persistence.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace ae::stream_matrix_bench {
namespace {

bool IsDigitsOnly(std::string const& s) {
  return !s.empty() &&
         std::all_of(s.begin(), s.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; });
}

}  // namespace

bool DomainStateHasPersistedObjects(
    std::filesystem::path const& state_dir) {
  std::error_code ec;
  if (!std::filesystem::exists(state_dir, ec) ||
      !std::filesystem::is_directory(state_dir, ec)) {
    return false;
  }
  for (auto const& entry :
       std::filesystem::directory_iterator(state_dir, ec)) {
    if (ec || !entry.is_directory()) {
      continue;
    }
    auto name = entry.path().filename().string();
    if (!IsDigitsOnly(name)) {
      continue;
    }
    for (auto const& class_entry :
         std::filesystem::directory_iterator(entry.path(), ec)) {
      if (!ec && class_entry.is_directory()) {
        return true;
      }
    }
  }
  return false;
}

std::optional<BootstrapMarker> ReadBootstrapMarker(
    std::filesystem::path const& path) {
  std::ifstream in(path);
  if (!in) {
    return std::nullopt;
  }
  BootstrapMarker m;
  std::string line;
  while (std::getline(in, line)) {
    auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    auto key = line.substr(0, eq);
    auto val = line.substr(eq + 1);
    if (key == "parent_uid") {
      m.parent_uid = val;
    } else if (key == "sender_client_id") {
      m.sender_client_id = val;
    } else if (key == "receiver_client_id") {
      m.receiver_client_id = val;
    } else if (key == "sender_uid") {
      m.sender_uid = val;
    } else if (key == "receiver_uid") {
      m.receiver_uid = val;
    }
  }
  if (m.sender_uid.empty() || m.receiver_uid.empty()) {
    return std::nullopt;
  }
  return m;
}

void WriteBootstrapMarker(std::filesystem::path const& path,
                          BootstrapMarker const& marker) {
  std::ofstream out(path, std::ios::trunc);
  out << "parent_uid=" << marker.parent_uid << '\n'
      << "sender_client_id=" << marker.sender_client_id << '\n'
      << "receiver_client_id=" << marker.receiver_client_id << '\n'
      << "sender_uid=" << marker.sender_uid << '\n'
      << "receiver_uid=" << marker.receiver_uid << '\n';
}

void WriteRegistrationStateMissing(std::ostream& out, char const* state_dir) {
  out << "REGISTRATION_STATE_MISSING state_dir=" << state_dir
      << " (run TCP bootstrap first; AE_FILTRATION required)\n";
}

}  // namespace ae::stream_matrix_bench
