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

#ifndef AETHER_TELE_TRAPS_IO_STREAM_TRAPS_H_
#define AETHER_TELE_TRAPS_IO_STREAM_TRAPS_H_

#include <mutex>
#include <cstdint>
#include <ostream>
#include <string_view>
#include <unordered_map>

#include "aether/common.h"

#include "aether/tele/itrap.h"

namespace ae::tele {

struct Metric {
  std::uint32_t invocations_count;
  std::uint32_t max_duration;
  std::uint32_t sum_duration;
  std::uint32_t min_duration;
};

class IoStreamTrap final : public ITrap {
 public:
  explicit IoStreamTrap(std::ostream& stream);
  ~IoStreamTrap() override;

  void AddInvoke(Tag const& tag, std::uint32_t count) override;
  void AddInvokeDuration(Tag const& tag, Duration duration) override;
  void OpenLogLine(Tag const& tag) override;
  void InvokeTime(TimePoint time) override;
  void WriteLevel(Level level) override;
  void WriteModule(Module const& module) override;
  void Location(std::string_view file, std::uint32_t line) override;
  void TagName(std::string_view name) override;
  void Blob(std::uint8_t const* data, std::size_t size) override;
  void CloseLogLine(Tag const& tag) override;
  void WriteEnvData(EnvData const& env_data) override;

  std::unordered_map<std::uint32_t, Metric> const& metrics() const {
    return metrics_;
  }

 private:
  void delimiter();

  std::mutex sync_lock_;
  std::ostream& stream_;
  std::unordered_map<std::uint32_t, Metric> metrics_;
};

}  // namespace ae::tele

#endif  // AETHER_TELE_TRAPS_IO_STREAM_TRAPS_H_
