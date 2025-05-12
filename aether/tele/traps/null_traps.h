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

#ifndef AETHER_TELE_TRAPS_NULL_TRAPS_H_
#define AETHER_TELE_TRAPS_NULL_TRAPS_H_

#include <cstdint>

#include "aether/common.h"
#include "aether/reflect/reflect.h"

#include "aether/tele/modules.h"
#include "aether/tele/declaration.h"

namespace ae::tele {
struct NullTrap {
  struct LogStream {
    void index(std::size_t /* index */) {}
    void start_time(TimePoint const& /* start */) {}
    void level(Level::underlined_t /* level */) {}
    void module(Module /* module */) {}
    void file(std::string_view /* file */) {}
    void line(std::uint32_t /* line */) {}
    void name(std::string_view /* name */) {}
    template <typename... TArgs>
    void blob(char const* /* format */, TArgs&&... /* args */) {}
  };

  LogStream log_stream(Declaration const& /* decl */) { return {}; }

  struct MetricStream {
    void add_count(uint32_t /* count */) {}
    void add_duration(uint32_t /* duration */) {}
  };

  MetricStream metric_stream(Declaration /* decl */) { return {}; }

  struct EnvStream {
    void platform_type(char const* /* platform_type */) {}
    void compiler(char const* /* compiler */) {}
    void compiler_version(char const* /* compiler_version */) {}
    void compilation_option(CompileOption const& /* opt */) {}
    void library_version(char const* /* library_version */) {}
    void api_version(char const* /* api_version */) {}
    void cpu_type(char const* /* cpu_type */) {}
    void endianness(std::uint8_t /* endianness */) {}
    void utmid(std::uint32_t /* utm_id */) {}
  };

  EnvStream env_stream() { return {}; }

  struct NullLock {
    void lock() {}
    void unlock() {}
    bool try_lock() { return true; }
  };

  NullLock sync() { return {}; }

  AE_REFLECT()
};
}  // namespace ae::tele

#endif  // AETHER_TELE_TRAPS_NULL_TRAPS_H_ */
