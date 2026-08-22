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

#ifndef AETHER_DELIVERY_WINDOW_BENCH_CLIENT_ROLE_H_
#define AETHER_DELIVERY_WINDOW_BENCH_CLIENT_ROLE_H_

#include <string>

#include "common/bench_types.h"

namespace ae::bench::dw {

struct ClientArgs {
  Side side{Side::kA};
  std::string run_id;
  std::string state_dir;
  std::string pipe_name;
  std::string client_name;
  std::string peer_uid;
  std::string trace_path;
  std::string parent_uid{"3ac93165-3d37-4970-87a6-fa4ee27744e4"};
  TimingConfig timing{};
  bool has_timing{false};
};

int RunClientRole(ClientArgs const& args);

}  // namespace ae::bench::dw

#endif  // AETHER_DELIVERY_WINDOW_BENCH_CLIENT_ROLE_H_
