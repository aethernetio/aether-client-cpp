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

#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_PERSISTENCE_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_PERSISTENCE_H_

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>

namespace ae::stream_matrix_bench {

inline constexpr char kDefaultDomainStateDir[] = "state";
inline constexpr char kBootstrapMarkerFile[] = "stream_matrix_bootstrap.ok";

struct BootstrapMarker {
  std::string parent_uid;
  std::string sender_client_id;
  std::string receiver_client_id;
  std::string sender_uid;
  std::string receiver_uid;
};

bool DomainStateHasPersistedObjects(
    std::filesystem::path const& state_dir = kDefaultDomainStateDir);

std::optional<BootstrapMarker> ReadBootstrapMarker(
    std::filesystem::path const& path);

void WriteBootstrapMarker(std::filesystem::path const& path,
                          BootstrapMarker const& marker);

void WriteRegistrationStateMissing(std::ostream& out,
                                   char const* state_dir);

}  // namespace ae::stream_matrix_bench

#endif
