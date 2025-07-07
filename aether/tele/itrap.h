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

#ifndef AETHER_TELE_ITRAP_H_
#define AETHER_TELE_ITRAP_H_

#ifndef AETHER_TELE_TELE_H_
#  error "Include tele.h instead"
#endif

#include <vector>
#include <cstdint>
#include <ostream>
#include <string_view>

#include "aether/common.h"
#include "aether/tele/tags.h"
#include "aether/tele/levels.h"
#include "aether/tele/modules.h"
#include "aether/tele/compile_option.h"

namespace ae::tele {

struct EnvData {
  std::string_view platform_type;
  std::string_view compiler;
  std::string_view compiler_version;
  std::string_view library_version;
  std::string_view api_version;
  std::string_view cpu_arch;
  std::uint8_t endianness;
  std::uint32_t utm_id;
  std::vector<CompileOption> compile_options;
  std::vector<CustomOption> custom_options;
};
/**
 * \brief Telemetry trap interaface.
 */
class ITrap {
 public:
  virtual ~ITrap() = default;

  /**
   * \brief Invoke count for metrics
   */
  virtual void AddInvoke(Tag const& tag, std::uint32_t count) = 0;
  /**
   * \brief Invoke duration for metrics
   */
  virtual void AddInvokeDuration(Tag const& tag, Duration duration) = 0;
  /**
   * \brief Open the log line to write.
   */
  virtual void OpenLogLine(Tag const& tag) = 0;
  /**
   * \brief Telemetry invoke time.
   */
  virtual void InvokeTime(TimePoint time) = 0;
  /**
   * \brief Telemetry tag level
   */
  virtual void WriteLevel(Level level) = 0;
  /**
   * \brief Tag module
   */
  virtual void WriteModule(Module const& module) = 0;
  /**
   * \brief Tag location
   */
  virtual void Location(std::string_view file, std::uint32_t line) = 0;
  /**
   * \brief Tag name
   */
  virtual void TagName(std::string_view name) = 0;
  /**
   * \brief Get stream to write telemetry blob data.
   */
  virtual void Blob(std::uint8_t const* data, std::size_t size) = 0;
  /**
   * \brief Close the log line
   */
  virtual void CloseLogLine(Tag const& tag) = 0;
  /**
   * \brief Write an environment data
   */
  virtual void WriteEnvData(EnvData const& env_data) = 0;
};
}  // namespace ae::tele

#endif  // AETHER_TELE_ITRAP_H_
