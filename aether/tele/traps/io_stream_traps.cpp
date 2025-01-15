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

#include "aether/tele/traps/io_stream_traps.h"

#include <ios>
#include <limits>
#include <iomanip>
#include <algorithm>

#include "aether/tele/ios.h"
#include "aether/tele/ios_time.h"

namespace ae::tele {

void IoStreamTrap::MetricsStream::add_count(uint32_t count) {
  metric_.invocations_count_ += count;
}

void IoStreamTrap::MetricsStream::add_duration(uint32_t duration) {
  metric_.max_duration_ = std::max(metric_.max_duration_, duration);

  metric_.sum_duration_ += duration;

  metric_.min_duration_ = std::min(metric_.min_duration_, duration);
}

IoStreamTrap::MetricsStream IoStreamTrap::metric_stream(
    Declaration const& decl) {
  auto [it, _] = metrics_.emplace(
      decl.index_, Metric{{}, {}, {}, std::numeric_limits<uint32_t>::max()});
  return MetricsStream{it->second};
}

IoStreamTrap::IoStreamTrap(std::ostream& stream) : stream_{stream} {}

IoStreamTrap::~IoStreamTrap() {
  stream_ << "Metrics:\n";
  for (auto const& [index, ms] : metrics_) {
    stream_ << Format(
        "\tidx:{}\n\t\tinv "
        "count:{}\n\t\tmax_duration:{}\n\t\tsum_duration:{}\n\t\tmin_duration:{"
        "}\n",
        index, ms.invocations_count_, ms.max_duration_, ms.sum_duration_,
        ms.min_duration_);
  }
  stream_ << "\n";
}

IoStreamTrap::LogStream::LogStream(std::ostream& stream) : stream_{stream} {}

IoStreamTrap::LogStream::LogStream(LogStream&& other) noexcept
    : stream_{other.stream_} {
  other.moved_ = true;
}

IoStreamTrap::LogStream::~LogStream() {
  if (moved_) {
    return;
  }
  stream_ << std::endl;
}

void IoStreamTrap::LogStream::index(std::size_t index) {
  delimeter();
  stream_ << std::setw(3) << std::left << std::dec << index << std::right
          << std::setw(0);
}
void IoStreamTrap::LogStream::start_time(TimePoint const& start) {
  delimeter();
  stream_ << FormatTimePoint("[%H:%M:%S]", start);
}
void IoStreamTrap::LogStream::level(Level::underlined_t level) {
  delimeter();
  Format(stream_, "{}", Level{level});
}
void IoStreamTrap::LogStream::module(Module::underlined_t module) {
  delimeter();
  Format(stream_, "{}", Module{module});
}
void IoStreamTrap::LogStream::file(char const* file) {
  delimeter();
  const char* filename = strrchr(file, '/');
  if (filename == nullptr) {
    filename = strrchr(file, '\\');
  }
  if (filename == nullptr) {
    filename = "UNKOWN FILE";
  } else {
    filename += 1;
  }
  stream_ << filename;
}
void IoStreamTrap::LogStream::line(std::uint32_t line) {
  delimeter();
  stream_ << line;
}
void IoStreamTrap::LogStream::name(char const* name) {
  delimeter();
  stream_ << name;
}

void IoStreamTrap::LogStream::delimeter() {
  if (!start_) {
    stream_ << std::setw(1) << ':' << std::setw(0);
  }
  start_ = false;
}

IoStreamTrap::LogStream IoStreamTrap::log_stream(
    Declaration const& /* decl */) {
  return LogStream{stream_};
}

void IoStreamTrap::EnvStream::platform_type(char const* platform_type) {
  stream_ << "Platform:" << platform_type << '\n';
}
void IoStreamTrap::EnvStream::compiler(char const* compiler) {
  stream_ << "Compiler:" << compiler << '\n';
}
void IoStreamTrap::EnvStream::compiler_version(char const* compiler_version) {
  stream_ << "Compiler version:" << compiler_version << '\n';
}
void IoStreamTrap::EnvStream::compilation_option(CompileOption const& opt) {
  stream_.write(opt.name_.data(),
                static_cast<std::streamsize>(opt.name_.size()));
  stream_ << ":" << opt.value_ << '\n';
}
void IoStreamTrap::EnvStream::library_version(char const* library_version) {
  stream_ << "Library version: " << library_version << '\n';
}
void IoStreamTrap::EnvStream::api_version(char const* api_version) {
  stream_ << "API version:" << api_version << '\n';
}

void IoStreamTrap::EnvStream::cpu_type(char const* cpu_type) {
  stream_ << "CPU type:" << cpu_type << '\n';
}
void IoStreamTrap::EnvStream::endianness(uint8_t endianness) {
  stream_ << "Endianness:"
          << (endianness == static_cast<uint8_t>(AE_LITTLE_ENDIAN)
                  ? "LittleEndian"
                  : "BigEndian")
          << '\n';
}

IoStreamTrap::EnvStream IoStreamTrap::env_stream() {
  return EnvStream{stream_};
}

}  // namespace ae::tele