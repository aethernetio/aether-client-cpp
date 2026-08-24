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

#ifndef AETHER_UAP_1S_TIMING_CHARACTERIZATION_COORDINATOR_H_
#define AETHER_UAP_1S_TIMING_CHARACTERIZATION_COORDINATOR_H_

#include <cstdint>
#include <string>

namespace ae::test_uap_ping_retry_window {

struct CharacterizationArgs {
  std::string run_id;
  std::string artifact_dir;
  std::string exe_path;
  std::string parent_uid{"3ac93165-3d37-4970-87a6-fa4ee27744e4"};
  std::string transport{"tcp"};
  std::int64_t ping_interval_ms{1000};
  std::int64_t receive_window_ms{250};
  std::uint32_t seed{1};
  int logical_cycles{100};
  int hard_stop_runs{20};
  int graceful_runs{20};
  int window_samples_main{30};
  int window_samples_extra{10};
  int loss_cases{0};
  bool quick{false};
  bool skip_long_characterization{false};
};

int RunCharacterization(CharacterizationArgs const& args);

}  // namespace ae::test_uap_ping_retry_window

#endif  // AETHER_UAP_1S_TIMING_CHARACTERIZATION_COORDINATOR_H_
