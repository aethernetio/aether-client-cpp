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

#include <cstdint>
#include <iostream>
#include <ostream>
#include <utility>
#include <string_view>
#include <unordered_map>

#include "aether/common.h"
#include "aether/format/format.h"
#include "aether/reflect/reflect.h"

#include "aether/tele/modules.h"
#include "aether/tele/declaration.h"

namespace ae::tele {

struct Metric {
  std::uint32_t invocations_count_;
  std::uint32_t max_duration_;
  std::uint32_t sum_duration_;
  std::uint32_t min_duration_;
};

struct IoStreamTrap {
  struct MetricsStream {
    void add_count(std::uint32_t count = 1);
    void add_duration(std::uint32_t duration);

    Metric& metric_;
  };

  struct LogStream {
    explicit LogStream(std::ostream& stream);
    LogStream(LogStream&& other) noexcept;
    ~LogStream();

    void index(std::uint32_t index);
    void start_time(TimePoint const& start);
    void level(Level::underlined_t level);
    void module(Module const& module);
    void file(std::string_view file);
    void line(std::uint32_t line);
    void name(std::string_view name);

    template <typename... TArgs>
    void blob(std::string_view format, TArgs&&... args) {
      delimeter();
      Format(stream_, format, std::forward<TArgs>(args)...);
    }
    void delimeter();

    std::ostream& stream_;
    bool start_{true};
    bool moved_{false};
  };

  struct EnvStream {
    std::ostream& stream_;

    void platform_type(char const* platform_type);
    void compiler(char const* compiler);
    void compiler_version(char const* compiler_version);
    void compilation_option(CompileOption const& opt);
    void library_version(char const* library_version);
    void api_version(char const* api_version);
    void cpu_type(char const* cpu_type);
    void endianness(uint8_t endianness);
  };

  // list of known declarations

  explicit IoStreamTrap(std::ostream& stream);
  ~IoStreamTrap();

  LogStream log_stream(Declaration const& decl);
  MetricsStream metric_stream(Declaration const& decl);
  EnvStream env_stream();

  std::ostream& stream_;
  std::unordered_map<std::size_t, Metric> metrics_;
};

}  // namespace ae::tele

#endif  // AETHER_TELE_TRAPS_IO_STREAM_TRAPS_H_
