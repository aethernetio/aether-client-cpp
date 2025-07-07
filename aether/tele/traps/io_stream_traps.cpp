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

#define AETHER_TELE_TELE_H_

#include "aether/tele/traps/io_stream_traps.h"

#include <ios>
#include <iomanip>
#include <algorithm>

#include "aether/format/format.h"

namespace ae::tele {

IoStreamTrap::IoStreamTrap(std::ostream& stream) : stream_{stream} {}

IoStreamTrap::~IoStreamTrap() {
  auto lock_guard = std::lock_guard{sync_lock_};
  stream_ << "Metrics:\n";
  Format(stream_, "Index,Invocations,Max Duration,Sum Duration,Min Duration\n");
  for (auto const& [index, ms] : metrics_) {
    Format(stream_, "{},{},{},{},{}\n", index, ms.invocations_count,
           ms.max_duration, ms.sum_duration, ms.min_duration);
  }
  stream_ << std::endl;
}

void IoStreamTrap::AddInvoke(Tag const& tag, std::uint32_t count) {
  auto lock_guard = std::lock_guard{sync_lock_};
  metrics_[tag.index()].invocations_count += count;
}

void IoStreamTrap::AddInvokeDuration(Tag const& tag, Duration duration) {
  auto lock_guard = std::lock_guard{sync_lock_};
  auto& metric = metrics_[tag.index()];
  metric.max_duration = std::max(metric.max_duration, duration.count());
  metric.sum_duration += duration.count();
  if (metric.min_duration == 0) {
    metric.min_duration = duration.count();
  } else {
    metric.min_duration = std::min(metric.min_duration, duration.count());
  }
}

void IoStreamTrap::OpenLogLine(Tag const& tag) {
  sync_lock_.lock();
  stream_ << std::setw(3) << std::right << std::dec << tag.index() << std::right
          << std::setw(0);
}

void IoStreamTrap::InvokeTime(TimePoint time) {
  delimiter();
  Format(stream_, "[{:%H:%M:%S}]", time);
}

void IoStreamTrap::WriteLevel(Level level) {
  delimiter();
  Format(stream_, "{}", level);
}

void IoStreamTrap::WriteModule(Module const& module) {
  delimiter();
  Format(stream_, "{}", module);
}

void IoStreamTrap::Location(std::string_view file, std::uint32_t line) {
  delimiter();
  auto pos = file.find_last_of("/\\");
  if (pos == std::string_view::npos) {
    file = "UNKNOWN FILE";
  }
  file = file.substr(pos + 1, file.size() - pos);
  stream_.write(file.data(), static_cast<std::streamsize>(file.size()));
  delimiter();
  stream_ << line;
}

void IoStreamTrap::TagName(std::string_view name) {
  delimiter();
  stream_.write(name.data(), static_cast<std::streamsize>(name.size()));
}

void IoStreamTrap::Blob(std::uint8_t const* data, std::size_t size) {
  delimiter();
  stream_.write(reinterpret_cast<char const*>(data),
                static_cast<std::streamsize>(size));
}

void IoStreamTrap::CloseLogLine(Tag const&) {
  sync_lock_.unlock();
  stream_ << std::endl;
}

void IoStreamTrap::WriteEnvData(EnvData const& env_data) {
  auto lock_guard = std::lock_guard{sync_lock_};
  Format(stream_,
         "Platform:{}\nCompiler:{}\nCompiler version:{}\nLibrary "
         "version:{}\nApi version:{}\nCPU arch:{}\nEndianness:{}\nUTMid:{}\n",
         env_data.platform_type, env_data.compiler, env_data.compiler_version,
         env_data.library_version, env_data.api_version, env_data.cpu_arch,
         (env_data.endianness == static_cast<std::uint8_t>(AE_LITTLE_ENDIAN)
              ? "LittleEndian"
              : "BigEndian"),
         env_data.utm_id);
  for (auto const& opt : env_data.compile_options) {
    Format(stream_, "{}:{}\n", opt.name, opt.value);
  }
  for (auto const& opt : env_data.custom_options) {
    Format(stream_, "{}:{}\n", opt.name, opt.value);
  }
  stream_ << std::endl;
}

void IoStreamTrap::delimiter() {
  stream_ << std::setw(1) << ':' << std::setw(0);
}
}  // namespace ae::tele
