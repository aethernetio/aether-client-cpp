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

#include <algorithm>

#include "aether-miscpp/format/format.h"

namespace ae::tele {
namespace {

void WriteUnformatted(std::ostream& stream, std::string_view value) {
  stream.write(value.data(), static_cast<std::streamsize>(value.size()));
}

}  // namespace

IoStreamTrap::IoStreamTrap(std::ostream& stream) : stream_{stream} {}

IoStreamTrap::~IoStreamTrap() {
  auto lock = std::scoped_lock{sync_lock_};
  stream_ << "Metrics:\n";
  stream_ << "Index,Invocations,Max Duration,Sum Duration,Min Duration\n";
  for (auto const& [index, ms] : metrics_) {
    Format(stream_, "{},{},{},{},{}\n", index, ms.invocations_count,
           ms.max_duration, ms.sum_duration, ms.min_duration);
  }
  stream_ << '\n';
  stream_.flush();
}

void IoStreamTrap::AddInvoke(Tag const& tag, std::uint32_t count) {
  auto lock = std::scoped_lock{sync_lock_};
  metrics_[tag.index()].invocations_count += count;
}

void IoStreamTrap::AddInvokeDuration(Tag const& tag, Duration duration) {
  auto lock = std::scoped_lock{sync_lock_};
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
  WritePaddedIndex(tag.index());
}

void IoStreamTrap::InvokeTime(TimePoint time) {
  Format(stream_, ":[{:time}]", time);
}

void IoStreamTrap::WriteLevel(Level level) {
  stream_.put(':');
  WriteUnformatted(stream_, Level::text(level.value_));
}

void IoStreamTrap::WriteModule(Module const& module) {
  stream_.put(':');
  WriteUnformatted(stream_, module.name);
}

void IoStreamTrap::Location(std::string_view file, std::uint32_t line) {
  auto pos = file.find_last_of("/\\");
  if (pos != std::string_view::npos) {
    file = file.substr(pos + 1, file.size() - pos);
  } else {
    file = "UNKNOWN FILE";
  }
  Format(stream_, ":{}:{}", file, line);
}

void IoStreamTrap::TagName(std::string_view name) {
  stream_.put(':');
  WriteUnformatted(stream_, name);
}

void IoStreamTrap::Blob(std::uint8_t const* data, std::size_t size) {
  stream_.put(':');
  stream_.write(reinterpret_cast<char const*>(data),
                static_cast<std::streamsize>(size));
}

void IoStreamTrap::CloseLogLine(Tag const&) {
  stream_ << std::endl;
  sync_lock_.unlock();
}

void IoStreamTrap::WriteEnvData(EnvData const& env_data) {
  auto lock = std::scoped_lock{sync_lock_};
  Format(
      stream_,
      "Platform:{}\nCompiler:{}\nCompiler version:{}\nLibrary version:{}\nApi "
      "version:{}\nCPU arch:{}\nEndianness:{}\nUTMid:{}\n",
      env_data.platform_type, env_data.compiler, env_data.compiler_version,
      env_data.library_version, env_data.api_version, env_data.cpu_arch,
      (env_data.endianness == static_cast<std::uint8_t>(AE_LITTLE_ENDIAN)
           ? "LittleEndian"
           : "BigEndian"),
      env_data.utm_id);
  constexpr auto option_format = FormatScheme{"{}:{}\n"};
  for (auto const& opt : env_data.compile_options) {
    Format(stream_, option_format, opt.name, opt.value);
  }
  for (auto const& opt : env_data.custom_options) {
    Format(stream_, option_format, opt.name, opt.value);
  }
  stream_ << '\n';
  stream_.flush();
}

void IoStreamTrap::WritePaddedIndex(std::uint32_t index) {
  if (index < 10) {
    stream_ << "  ";
  } else if (index < 100) {
    stream_ << ' ';
  }
  Format(stream_, "{}", index);
}
}  // namespace ae::tele
