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
#include <functional>

#include "aether-miscpp/format/format.h"

namespace ae::tele {
namespace {

void WriteUnformatted(std::ostream& stream, std::string_view value) {
  stream.write(value.data(), static_cast<std::streamsize>(value.size()));
}

void WritePaddedIndex(std::ostream& stream, std::uint32_t index) {
  static constexpr auto kPaddings = "    ";
  static constexpr auto kSteps = 3;

  auto decs = std::invoke([&]() noexcept {
    std::uint32_t delim = 10;
    for (auto i = 0; i < kSteps; ++i) {
      auto d = index / delim;
      if (d == 0) {
        return i;
      }
      delim = delim * delim;
    }
    return kSteps;
  });
  stream.write(kPaddings, kSteps - decs);
  Format(stream, "{}", index);
}

}  // namespace

IoStreamTrap::LogLineWriter::LogLineWriter(std::ostream& s, Tag const& tag)
    : stream_{s} {
  WritePaddedIndex(stream_, tag.index());
}

void IoStreamTrap::LogLineWriter::InvokeTime(TimePoint time) {
  Format(stream_, ":[{:time}]", time);
}
void IoStreamTrap::LogLineWriter::WriteLevel(Level level) {
  stream_.put(':');
  WriteUnformatted(stream_, level.Text());
}
void IoStreamTrap::LogLineWriter::WriteModule(Module const& module) {
  stream_.put(':');
  WriteUnformatted(stream_, module.name);
}
void IoStreamTrap::LogLineWriter::Location(std::string_view file,
                                           std::uint32_t line) {
  auto pos = file.find_last_of("/\\");
  if (pos != std::string_view::npos) {
    file = file.substr(pos + 1, file.size() - pos);
  } else {
    file = "UNKNOWN FILE";
  }
  Format(stream_, ":{}:{}", file, line);
}
void IoStreamTrap::LogLineWriter::TagName(std::string_view name) {
  stream_.put(':');
  WriteUnformatted(stream_, name);
}
void IoStreamTrap::LogLineWriter::Blob(std::span<std::uint8_t const> blob) {
  if (blob.empty()) {
    return;
  }
  stream_.put(':');
  stream_.write(reinterpret_cast<char const*>(blob.data()),
                static_cast<std::streamsize>(blob.size()));
}

IoStreamTrap::IoStreamTrap(std::ostream& stream) : stream_{stream} {}

IoStreamTrap::~IoStreamTrap() {
  auto lock = std::scoped_lock{sync_lock_};
  WriteUnformatted(stream_, "Metrics:\n");
  WriteUnformatted(
      stream_, "Index,Invocations,Max Duration,Sum Duration,Min Duration\n");
  for (auto const& [index, ms] : metrics_) {
    Format(stream_, "{},{},{},{},{}\n", index, ms.invocations_count,
           ms.max_duration, ms.sum_duration, ms.min_duration);
  }
  stream_.put('\n');
  stream_.flush();
}

void IoStreamTrap::AddInvoke(Tag const& tag, std::uint32_t count) {
  auto lock = std::scoped_lock{sync_lock_};
  metrics_[tag.index()].invocations_count += count;
}

void IoStreamTrap::AddInvokeDuration(Tag const& tag, Duration duration) {
  auto lock = std::scoped_lock{sync_lock_};
  auto& metric = metrics_[tag.index()];
  metric.max_duration = std::max(metric.max_duration,
                                 static_cast<std::uint32_t>(duration.count()));
  metric.sum_duration += duration.count();
  if (metric.min_duration == 0) {
    metric.min_duration = static_cast<std::uint32_t>(duration.count());
  } else {
    metric.min_duration = std::min(
        metric.min_duration, static_cast<std::uint32_t>(duration.count()));
  }
}

void IoStreamTrap::LogLine(Tag const& tag, ILogCollector& log_collector) {
  auto lock = std::scoped_lock{sync_lock_};
  auto log_line = LogLineWriter(stream_, tag);
  log_collector.WriteLine(log_line);
  stream_.put('\n');
  stream_.flush();
}

void IoStreamTrap::WriteEnvData(EnvData const& env_data) {
  auto lock = std::scoped_lock{sync_lock_};
  Format(
      stream_,
      "Platform:{}\nCompiler:{}\nCompiler version:{}\nLibrary version:{}\nApi "
      "version:{}\nCPU arch:{}\nEndianness:{}\nUTMid:{}\n",
      env_data.platform_type, env_data.compiler, env_data.compiler_version,
      env_data.library_version, env_data.api_version, env_data.cpu_arch,
      (env_data.endianness == Endianness::Little ? "LittleEndian"
                                                 : "BigEndian"),
      env_data.utm_id);
  constexpr auto option_format = FormatScheme{"{}:{}\n"};
  for (auto const& opt : env_data.compile_options) {
    Format(stream_, option_format, opt.name, opt.value);
  }
  for (auto const& opt : env_data.custom_options) {
    Format(stream_, option_format, opt.name, opt.value);
  }
  stream_.put('\n');
  stream_.flush();
}

}  // namespace ae::tele
