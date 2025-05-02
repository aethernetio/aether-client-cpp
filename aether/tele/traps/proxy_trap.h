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

#ifndef AETHER_TELE_TRAPS_PROXY_TRAP_H_
#define AETHER_TELE_TRAPS_PROXY_TRAP_H_

#include <utility>
#include <string_view>

#include "aether/common.h"
#include "aether/ptr/rc_ptr.h"
#include "aether/reflect/reflect.h"

#include "aether/tele/modules.h"
#include "aether/tele/declaration.h"

namespace ae::tele {
template <typename TrapFirst, typename TrapSecond>
class ProxyTrap {
 public:
  struct LogStream {
    using Trap1Stream = decltype(std::declval<TrapFirst>().log_stream(
        std::declval<Declaration>()));
    using Trap2Stream = decltype(std::declval<TrapSecond>().log_stream(
        std::declval<Declaration>()));

    Trap1Stream stream1_;
    Trap2Stream stream2_;

    LogStream(Trap1Stream&& stream1, Trap2Stream&& stream2)
        : stream1_(std::move(stream1)), stream2_(std::move(stream2)) {}

    void index(std::uint32_t index) {
      stream1_.index(index);
      stream2_.index(index);
    }
    void start_time(TimePoint const& start) {
      stream1_.start_time(start);
      stream2_.start_time(start);
    }
    void level(Level::underlined_t level) {
      stream1_.level(level);
      stream2_.level(level);
    }
    void module(Module const& module) {
      stream1_.module(module);
      stream2_.module(module);
    }
    void file(std::string_view file) {
      stream1_.file(file);
      stream2_.file(file);
    }
    void line(std::uint32_t line) {
      stream1_.line(line);
      stream2_.line(line);
    }
    void name(std::string_view name) {
      stream1_.name(name);
      stream2_.name(name);
    }

    template <typename... TArgs>
    void blob(std::string_view format, TArgs const&... args) {
      stream1_.blob(format, args...);
      stream2_.blob(format, args...);
    }
  };

  struct MetricStream {
    using Trap1Stream = decltype(std::declval<TrapFirst>().metric_stream(
        std::declval<Declaration>()));
    using Trap2Stream = decltype(std::declval<TrapSecond>().metric_stream(
        std::declval<Declaration>()));

    Trap1Stream stream1_;
    Trap2Stream stream2_;

    MetricStream(Trap1Stream&& stream1, Trap2Stream&& stream2)
        : stream1_(std::move(stream1)), stream2_(std::move(stream2)) {}

    void add_count(uint32_t count) {
      stream1_.add_count(count);
      stream2_.add_count(count);
    }
    void add_duration(uint32_t duration) {
      stream1_.add_duration(duration);
      stream2_.add_duration(duration);
    }
  };

  struct EnvStream {
    using Trap1Stream = decltype(std::declval<TrapFirst>().env_stream());
    using Trap2Stream = decltype(std::declval<TrapSecond>().env_stream());

    Trap1Stream stream1_;
    Trap2Stream stream2_;

    EnvStream(Trap1Stream&& stream1, Trap2Stream&& stream2)
        : stream1_(std::move(stream1)), stream2_(std::move(stream2)) {}

    void platform_type(char const* platform_type) {
      stream1_.platform_type(platform_type);
      stream2_.platform_type(platform_type);
    }
    void compiler(char const* compiler) {
      stream1_.compiler(compiler);
      stream2_.compiler(compiler);
    }
    void compiler_version(char const* compiler_version) {
      stream1_.compiler_version(compiler_version);
      stream2_.compiler_version(compiler_version);
    }
    void compilation_option(CompileOption const& opt) {
      stream1_.compilation_option(opt);
      stream2_.compilation_option(opt);
    }
    void library_version(char const* library_version) {
      stream1_.library_version(library_version);
      stream2_.library_version(library_version);
    }
    void api_version(char const* api_version) {
      stream1_.api_version(api_version);
      stream2_.api_version(api_version);
    }
    void cpu_type(char const* cpu_type) {
      stream1_.cpu_type(cpu_type);
      stream2_.cpu_type(cpu_type);
    }
    void endianness(std::uint8_t endianness) {
      stream1_.endianness(endianness);
      stream2_.endianness(endianness);
    }
    void utmid(std::uint32_t utm_id) {
      stream1_.utmid(utm_id);
      stream2_.utmid(utm_id);
    }
  };

  struct ProxySync {
    using Trap1Sync = decltype(std::declval<TrapFirst>().sync());
    using Trap2Sync = decltype(std::declval<TrapSecond>().sync());

    void lock() {
      sync1.lock();
      sync2.lock();
    }
    void unlock() {
      sync1.unlock();
      sync2.unlock();
    }
    bool try_lock() {
      if (sync1.try_lock()) {
        if (sync2.try_lock()) {
          return true;
        }
        sync1.unlock();
      }
      return false;
    }

    Trap1Sync sync1;
    Trap2Sync sync2;
  };

  ProxyTrap(RcPtr<TrapFirst> trap1, RcPtr<TrapSecond> trap2)
      : first{std::move(trap1)}, second{std::move(trap2)} {}

  LogStream log_stream(Declaration decl) {
    return LogStream{first->log_stream(decl), second->log_stream(decl)};
  }

  MetricStream metric_stream(Declaration decl) {
    return MetricStream{first->metric_stream(decl),
                        second->metric_stream(decl)};
  }

  EnvStream env_stream() {
    return EnvStream{first->env_stream(), second->env_stream()};
  }

  ProxySync sync() { return ProxySync{first->sync(), second->sync()}; }

  RcPtr<TrapFirst> first;
  RcPtr<TrapSecond> second;
};
}  // namespace ae::tele

#endif  // AETHER_TELE_TRAPS_PROXY_TRAP_H_
