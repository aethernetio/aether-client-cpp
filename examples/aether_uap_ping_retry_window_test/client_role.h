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

#ifndef AETHER_UAP_PING_RETRY_WINDOW_TEST_CLIENT_ROLE_H_
#define AETHER_UAP_PING_RETRY_WINDOW_TEST_CLIENT_ROLE_H_

#include <cstdint>
#include <string>

namespace ae::test_uap_ping_retry_window {

enum class Side : std::uint8_t { kA = 0, kB = 1 };

struct ClientArgs {
  Side side{Side::kA};
  std::string run_id;
  std::string state_dir;
  std::string pipe_name;
  std::string client_name;
  std::string artifact_dir;
  std::string parent_uid{"3ac93165-3d37-4970-87a6-fa4ee27744e4"};
  std::int64_t ping_interval_ms{3000};
  std::int64_t receive_window_ms{1000};
};

int RunClientRole(ClientArgs const& args);

}  // namespace ae::test_uap_ping_retry_window

#endif  // AETHER_UAP_PING_RETRY_WINDOW_TEST_CLIENT_ROLE_H_
